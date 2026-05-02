#include "command_router.hpp"
#include "serial/serial_protocol.hpp"

void CommandRouter::on(pb_size_t which_body, CommandHandler handler, void* ctx) {
    if (routeCount < MAX_ROUTES) {
        routes[routeCount++] = {which_body, handler, ctx};
    }
}

void CommandRouter::poll() {
    dc_Command cmd = dc_Command_init_zero;
    if (!SerialProtocol::readCommand(cmd)) {
        return;
    }

    for (uint8_t i = 0; i < routeCount; i++) {
        if (routes[i].which_body == cmd.which_body) {
            routes[i].handler(routes[i].ctx, cmd);
            return;
        }
    }

    // No route matched
    SerialProtocol::sendResponse(cmd.id, false);
}
