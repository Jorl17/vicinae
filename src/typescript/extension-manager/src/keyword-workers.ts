import * as fsp from "node:fs/promises";
import type { Worker } from "node:worker_threads";
import { createLaunchPayload } from "./launch-payload";
import type { Logger } from "./logger";
import type * as manager from "./proto/manager";
import * as extension from "./proto/manager-extension";

const IDLE_TIMEOUT_MS = 10 * 60 * 1000;
// A handler can make a network request, for example to find the external IP address.
const QUERY_TIMEOUT_MS = 10_000;

// Return undefined, after calling onTimeout, when the promise takes too long.
const withTimeout = async <T>(
	promise: Promise<T>,
	onTimeout: () => void,
): Promise<T | undefined> => {
	let timeout: NodeJS.Timeout | undefined;
	const expired = new Promise<undefined>((resolve) => {
		timeout = setTimeout(() => {
			onTimeout();
			resolve(undefined);
		}, QUERY_TIMEOUT_MS);
	});

	try {
		return await Promise.race([promise, expired]);
	} finally {
		clearTimeout(timeout);
	}
};

type KeywordWorker = {
	worker: Worker;
	client: extension.Client;
	// The session of the last query, which receives the API calls of the handler.
	sessionId?: string;
	// the modification time of the handler file when we started the worker
	builtAt: number;
	idleTimer?: NodeJS.Timeout;
};

/**
 * The workers that run the keyword handlers of commands while the user types in
 * the root search. Each command gets its own worker, so that a handler can keep
 * its data loaded between key presses. We stop a worker after ten minutes
 * without queries.
 *
 * A handler can call the Vicinae API, as a loaded command does: we send its
 * messages to Vicinae as messages of the session of the last query (see
 * queryKeyword in manager.fig), and Vicinae answers through message().
 */
export class KeywordWorkers {
	constructor(
		private readonly spawn: () => Worker,
		private readonly logger: Logger,
		private readonly sendToVicinae: (sessionId: string, data: string) => void,
	) {}

	async query(
		sessionId: string,
		load: manager.LoadOptions,
		query: string,
	): Promise<manager.KeywordRows> {
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
		if (this.latest.get(key) !== sequence) return { items: [] };

		entry.sessionId = sessionId;
		clearTimeout(entry.idleTimer);
		entry.idleTimer = setTimeout(() => this.stop(key), IDLE_TIMEOUT_MS);

		const rows = await withTimeout(
			entry.client.Lifecycle.query_keyword(data, query),
			() => this.logger.error(`keyword query for ${key} timed out`),
		);
		return (
			rows ?? {
				items: [],
				error: `The command did not answer within ${QUERY_TIMEOUT_MS / 1000} seconds`,
			}
		);
	}

	/** Pass a message from Vicinae to the worker whose handler runs in its session. */
	message(sessionId: string, data: string): boolean {
		for (const entry of this.workers.values()) {
			if (entry.sessionId !== sessionId) continue;
			entry.client.Lifecycle.send_message(data);
			return true;
		}
		return false;
	}

	/** Run an action from the rows of the last query of a command. */
	async runAction(
		load: manager.LoadOptions,
		id: string,
	): Promise<manager.KeywordActionResult> {
		const entry = this.workers.get(`${load.extension_id}:${load.command_name}`);
		if (!entry) return { error: "These results are out of date" };

		const result = await withTimeout(
			entry.client.Lifecycle.run_keyword_action(id),
			() => {},
		);
		return result ?? { error: "The action did not finish in time" };
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

		// As for a loaded command, a message is either the answer of the worker to
		// the manager or a call of the Vicinae API.
		worker.on("message", (data) => {
			entry.client.route(data);
			if (entry.sessionId) this.sendToVicinae(entry.sessionId, data);
		});
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
