/**
 * Keyword handlers show rows in the root search while the user types. A command
 * declares its keyword with the `keyword` field in the manifest, and its handler
 * is the default export of `src/<command name>.keyword.ts`. When the user types
 * the keyword, a space and a query, for example `calc 2*(3+4)`, Vicinae calls
 * the handler with the query on every key press and shows the rows it returns
 * above the other results. Enter runs the first action of the selected row.
 *
 * This is a Vicinae extension of the Raycast API.
 */

export type KeywordAction = {
	/** `copy` copies `content` to the clipboard, `paste` pastes it into the focused window. */
	type: "copy" | "paste";
	content: string;
	/** The title in the action panel. Vicinae chooses one when it is missing. */
	title?: string;
};

export type KeywordItem = {
	title: string;
	subtitle?: string;
	/** An emoji, a built-in icon name such as `Icon.Calculator`, or a file in the assets folder. */
	icon?: string;
	/** A short text at the right of the row. */
	accessory?: string;
	/** The first action is the primary action, which Enter runs. */
	actions: KeywordAction[];
};

export type KeywordHandlerContext = {
	/** Aborted when the user types again before the handler returns. */
	signal: AbortSignal;
};

export type KeywordHandler = (
	query: string,
	context: KeywordHandlerContext,
) => KeywordItem[] | Promise<KeywordItem[]>;
