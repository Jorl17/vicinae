import * as fsp from "node:fs/promises";
import type { Worker } from "node:worker_threads";
import { createLaunchPayload } from "./launch-payload";
import type { Logger } from "./logger";
import type * as manager from "./proto/manager";
import * as extension from "./proto/manager-extension";

const IDLE_TIMEOUT_MS = 10 * 60 * 1000;
const QUERY_TIMEOUT_MS = 5000;

type KeywordWorker = {
	worker: Worker;
	client: extension.Client;
	// the modification time of the handler file when we started the worker
	builtAt: number;
	idleTimer?: NodeJS.Timeout;
};

/**
 * The workers that run the keyword handlers of commands while the user types in
 * the root search. Each command gets its own worker, so that a handler can keep
 * its data loaded between key presses. We stop a worker after ten minutes
 * without queries.
 */
export class KeywordWorkers {
	constructor(
		private readonly spawn: () => Worker,
		private readonly logger: Logger,
	) {}

	async query(
		load: manager.LoadOptions,
		query: string,
	): Promise<manager.KeywordItem[]> {
		const key = `${load.extension_id}:${load.command_name}`;
		const sequence = (this.latest.get(key) ?? 0) + 1;
		this.latest.set(key, sequence);
		const builtAt = (await fsp.stat(load.entrypoint)).mtimeMs;
		let entry = this.workers.get(key);

		// After a new build of the extension, run the new handler.
		if (entry && entry.builtAt !== builtAt) this.stop(key);
		entry = this.workers.get(key) ?? this.start(key, builtAt);
		const { data } = await createLaunchPayload(load);

		// The file operations above finish in any order, so a newer query can get
		// here first. We send only the newest query to the worker.
		if (this.latest.get(key) !== sequence) return [];

		clearTimeout(entry.idleTimer);
		entry.idleTimer = setTimeout(() => this.stop(key), IDLE_TIMEOUT_MS);

		let timeout: NodeJS.Timeout | undefined;
		const expired = new Promise<manager.KeywordItem[]>((resolve) => {
			timeout = setTimeout(() => {
				this.logger.error(`keyword query for ${key} timed out`);
				resolve([]);
			}, QUERY_TIMEOUT_MS);
		});

		try {
			return await Promise.race([
				entry.client.Lifecycle.query_keyword(data, query).then(
					(rows) => rows.items,
				),
				expired,
			]);
		} finally {
			clearTimeout(timeout);
		}
	}

	private start(key: string, builtAt: number): KeywordWorker {
		const worker = this.spawn();
		const transport = new extension.RpcTransport({
			send: (data) => worker.postMessage(data),
		});
		const entry: KeywordWorker = {
			worker,
			client: new extension.Client(transport),
			builtAt,
		};

		worker.on("message", (data) => entry.client.route(data));
		worker.stdout.resume();
		worker.stderr.on("data", (buf: Buffer) => {
			this.logger.error(`${key}: ${buf.toString().trimEnd()}`);
		});
		worker.on("error", (error) => {
			this.logger.error(`keyword worker ${key}: ${error.stack}`);
		});
		worker.on("exit", () => {
			clearTimeout(entry.idleTimer);
			if (this.workers.get(key) === entry) this.workers.delete(key);
		});

		this.workers.set(key, entry);
		this.logger.info(`Started keyword worker for ${key}`);

		return entry;
	}

	private stop(key: string) {
		const entry = this.workers.get(key);
		if (!entry) return;
		this.workers.delete(key);
		entry.worker.terminate();
		this.logger.info(`Stopped keyword worker for ${key}`);
	}

	private readonly workers = new Map<string, KeywordWorker>();
	private readonly latest = new Map<string, number>();
}
