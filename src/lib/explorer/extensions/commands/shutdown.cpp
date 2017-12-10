/**
 * Copyright (c) 2016-2017 mvs developers 
 *
 * This file is part of metaverse-explorer.
 *
 * metaverse-explorer is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <jsoncpp/json/json.h>

#include <metaverse/bitcoin.hpp>
#include <metaverse/explorer/define.hpp>
#include <metaverse/explorer/extensions/commands/shutdown.hpp>
#include <metaverse/explorer/extensions/command_extension_func.hpp>
#include <metaverse/explorer/extensions/exception.hpp>

namespace libbitcoin {
namespace explorer {
namespace commands {

/************************ shutdown *************************/
console_result shutdown::invoke (std::ostream& output,
        std::ostream& cerr, libbitcoin::server::server_node& node)
{
    auto& blockchain = node.chain_impl();

    // administrator_required option is true
    if (node.server_settings().administrator_required) {
        if(!blockchain.is_admin_account(auth_.name))
            throw account_authority_exception{"account empty or not administrator!"};
        blockchain.is_account_passwd_valid(auth_.name, auth_.auth);
    }

#ifndef _WIN32
    output << "sending SIGTERM to mvsd.";
    killpg(getpgrp(),SIGTERM);
#else
    output << "not support for Windows";
#endif

    return console_result::okay;
}


} // namespace commands
} // namespace explorer
} // namespace libbitcoin

