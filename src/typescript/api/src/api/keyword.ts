/**
 * Keyword handlers show rows in the root search while the user types. A command
 * declares its keyword with the `keyword` field in the manifest, and its handler
 * is the default export of `src/<command name>.keyword.ts`. When the user types
 * the keyword, a space and a query, for example `calc 2*(3+4)`, Vicinae calls
 * the handler with the query on every key press and shows the rows it returns
 * above the other results. Enter runs the first action of the selected row.
 *
 * The `keywordOptions` field of the command sets whether the query is required,
 * whether a space must follow the keyword, the row that Vicinae shows before the
 * handler returns, and when to run the handler, with the meaning of the same
 * settings of an Alfred script filter.
 *
 * A handler can call the rest of the API, for example `getFrontmostApplication`,
 * which gives the app that was in front before Vicinae opened. When the handler
 * returns no rows, the keyword row says "No results", and when it throws, the
 * keyword row shows the error message.
 *
 * This is a Vicinae extension of the Raycast API.
 */

export type KeywordAction =
	| {
			/** `copy` copies `content` to the clipboard, `paste` pastes it into the focused window. */
			type: "copy" | "paste";
			content: string;
			/** The title in the action panel. Vicinae chooses one when it is missing. */
			title?: string;
	  }
	| {
			/**
			 * `run` calls `run` in the extension. When it returns a text, Vicinae shows
			 * the text in a HUD. When it throws, Vicinae shows the error message.
			 */
			type: "run";
			title: string;
			run: () => void | string | Promise<void | string>;
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
