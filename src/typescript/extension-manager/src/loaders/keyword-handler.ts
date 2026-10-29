import type { KeywordAction, KeywordHandler, KeywordItem } from "@vicinae/api";
import { pathToFileURL } from "node:url";
import type * as proto from "../proto/extension-manager";

type RunAction = Extract<KeywordAction, { type: "run" }>["run"];

// We send every row to Vicinae on each key press, so we keep at most 100 rows.
const MAX_ITEMS = 100;

const loadHandler = async (entrypoint: string): Promise<KeywordHandler> => {
	const module = await import(pathToFileURL(entrypoint).href);
	const handler = module.default.default;

	if (typeof handler !== "function") {
		throw new Error(
			`keyword handler ${entrypoint} does not export a function as its default export`,
		);
	}

	return handler;
};

const errorMessage = (error: unknown) =>
	error instanceof Error ? error.message : String(error);

/**
 * Run the keyword handler of one command. A new query aborts the signal of the
 * query before it, and we return no rows for the aborted query. When the handler
 * fails, we return its error, so that Vicinae can show it. We keep the
 * functions of the run actions in the rows of the last query, so that Vicinae
 * can run one of them by its id.
 */
export class KeywordQueries {
	async run(entrypoint: string, query: string): Promise<proto.KeywordRows> {
		this.current?.abort();
		const controller = new AbortController();
		this.current = controller;

		try {
			this.handler ??= loadHandler(entrypoint).catch((error) => {
				this.handler = undefined;
				throw error;
			});
			const handler = await this.handler;
			if (controller.signal.aborted) return { items: [] };
			const items = await handler(query, { signal: controller.signal });
			if (controller.signal.aborted) return { items: [] };
			return { items: this.toProtocol(items ?? []) };
		} catch (error) {
			if (controller.signal.aborted) return { items: [] };
			console.error(error);
			return { items: [], error: errorMessage(error) };
		}
	}

	async runAction(id: string): Promise<proto.KeywordActionResult> {
		const action = this.runActions.get(id);
		if (!action) return { error: "These results are out of date" };

		try {
			const hud = await action();
			return { hud: typeof hud === "string" ? hud : undefined };
		} catch (error) {
			return { error: errorMessage(error) };
		}
	}

	// Keep only the fields of the protocol, so that a handler that returns
	// something else cannot break the message to Vicinae.
	private toProtocol(items: KeywordItem[]): proto.KeywordItem[] {
		this.runActions.clear();
		++this.queryNumber;

		return items
			.filter((item) => typeof item?.title === "string")
			.slice(0, MAX_ITEMS)
			.map((item) => ({
				title: item.title,
				subtitle: item.subtitle,
				icon: item.icon,
				accessory: item.accessory,
				actions: (item.actions ?? []).flatMap((action) =>
					this.actionToProtocol(action),
				),
			}));
	}

	private actionToProtocol(action: KeywordAction): proto.KeywordAction[] {
		switch (action?.type) {
			case "copy":
				return [
					{
						type: "Copy",
						content: String(action.content),
						title: action.title,
					},
				];
			case "paste":
				return [
					{
						type: "Paste",
						content: String(action.content),
						title: action.title,
					},
				];
			case "run": {
				const id = `${this.queryNumber}:${this.runActions.size}`;
				this.runActions.set(id, action.run);
				return [{ type: "Run", content: id, title: action.title }];
			}
			default:
				return [];
		}
	}

	private handler?: Promise<KeywordHandler>;
	private current?: AbortController;
	private readonly runActions = new Map<string, RunAction>();
	// part of the ids of run actions, so that an id from older rows finds no action
	private queryNumber = 0;
}
