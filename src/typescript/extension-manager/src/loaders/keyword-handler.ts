import type { KeywordHandler, KeywordItem } from "@vicinae/api";
import { pathToFileURL } from "node:url";
import type * as proto from "../proto/extension-manager";

const ACTION_TYPES = {
	copy: "Copy",
	paste: "Paste",
} as const satisfies Record<string, proto.KeywordActionType>;

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

// Keep only the fields of the protocol, so that a handler that returns
// something else cannot break the message to Vicinae.
const toProtocol = (items: KeywordItem[]): proto.KeywordItem[] =>
	items
		.filter((item) => typeof item?.title === "string")
		.slice(0, MAX_ITEMS)
		.map((item) => ({
			title: item.title,
			subtitle: item.subtitle,
			icon: item.icon,
			accessory: item.accessory,
			actions: (item.actions ?? [])
				.filter((action) => action.type in ACTION_TYPES)
				.map((action) => ({
					type: ACTION_TYPES[action.type],
					content: String(action.content),
					title: action.title,
				})),
		}));

/**
 * Run the keyword handler of one command. A new query aborts the signal of the
 * query before it, and we return no rows for the aborted query.
 */
export class KeywordQueries {
	async run(entrypoint: string, query: string): Promise<proto.KeywordItem[]> {
		this.current?.abort();
		const controller = new AbortController();
		this.current = controller;

		try {
			this.handler ??= loadHandler(entrypoint).catch((error) => {
				this.handler = undefined;
				throw error;
			});
			const handler = await this.handler;
			if (controller.signal.aborted) return [];
			const items = await handler(query, { signal: controller.signal });
			return controller.signal.aborted ? [] : toProtocol(items ?? []);
		} catch (error) {
			if (!controller.signal.aborted) console.error(error);
			return [];
		}
	}

	private handler?: Promise<KeywordHandler>;
	private current?: AbortController;
}
