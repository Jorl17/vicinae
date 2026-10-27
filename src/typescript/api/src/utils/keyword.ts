import { existsSync } from "node:fs";
import { join } from "node:path";

/**
 * Return the keyword handler of a command, `src/<command name>.keyword.ts`, in a
 * list for the esbuild entry points. The list is empty when the command has no
 * keyword.
 */
export const keywordEntryPoints = (
	command: { name: string; keyword?: string },
	root: string,
): string[] => {
	if (!command.keyword) return [];

	const source = join(root, "src", `${command.name}.keyword.ts`);

	if (!existsSync(source)) {
		throw new Error(
			`could not find the keyword handler src/${command.name}.keyword.ts for command ${command.name}.`,
		);
	}

	return [source];
};
