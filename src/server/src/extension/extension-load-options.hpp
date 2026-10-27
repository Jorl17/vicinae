#pragma once
#include "generated/manager.hpp"

class ExtensionCommand;
class ServiceRegistry;

/* Return the options to run a command in the extension manager: the command, its
 * extension, its preference values and the capabilities of this system. The
 * caller sets the fields that belong to one launch, such as the arguments. */
manager::LoadOptions extensionLoadOptions(const ExtensionCommand &command, const ServiceRegistry &services);
