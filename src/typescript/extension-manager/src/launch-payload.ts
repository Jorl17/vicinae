import * as fsp from "node:fs/promises";
import * as path from "node:path";
import type * as manager from "./proto/manager";
import type * as extension from "./proto/manager-extension";

export type LaunchPayload = {
	data: extension.LaunchEventData;
	// folder for the log streams and the pid file of the CLI
	supportInternalPath: string;
};

/** Create the support and asset folders of the extension and return the data
 * that a worker needs to run one of its commands. */
export const createLaunchPayload = async (
	load: manager.LoadOptions,
): Promise<LaunchPayload> => {
	const supportPath = path.join(
		load.vicinae_path,
		"support",
		load.extension_id,
	);
	const supportInternalPath = path.join(supportPath, ".vicinae");
	const assetsPath = path.join(
		load.vicinae_path,
		"extensions",
		load.extension_id,
		"assets",
	);

	await Promise.all([
		fsp.mkdir(assetsPath, { recursive: true }),
		fsp.mkdir(supportInternalPath, { recursive: true }),
	]);

	return {
		supportInternalPath,
		data: {
			entrypoint: load.entrypoint,
			argumentValues: load.arguments,
			preferenceValues: load.preferences,
			launch_context: load.launch_context,
			mode: load.mode,
			support_path: supportPath,
			asset_path: assetsPath,
			is_raycast: load.is_raycast,
			extension_name: load.extension_name,
			owner_or_author_name: load.owner_or_author_name,
			command_name: load.command_name,
			launch_type: load.launch_type,
			capabilities: load.capabilities,
			cwd: load.cwd,
			fallbackText: load.fallbackText,
		},
	};
};
