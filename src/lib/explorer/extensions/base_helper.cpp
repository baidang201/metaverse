/**
 * Copyright (c) 2016-2018 mvs developers 
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

#include <metaverse/explorer/commands/offline_commands_impl.hpp>
#include <metaverse/explorer/extensions/base_helper.hpp>
#include <metaverse/explorer/dispatch.hpp>
#include <metaverse/explorer/extensions/exception.hpp>
#include <unordered_map>


namespace libbitcoin {
namespace explorer {
namespace commands {

utxo_attach_type get_utxo_attach_type(const chain::output& output_)
{
    auto& output = const_cast<chain::output&>(output_);
    if (output.is_etp()) {
        return utxo_attach_type::etp;
    }
    if (output.is_asset_transfer()) {
        return utxo_attach_type::asset_transfer;
    }
    if (output.is_asset_issue()) {
        return utxo_attach_type::asset_issue;
    }
    if (output.is_asset_secondaryissue()) {
        return utxo_attach_type::asset_secondaryissue;
    }
    if (output.is_asset_cert()) {
        return utxo_attach_type::asset_cert;
    }
    if (output.is_did_issue()) {
        return utxo_attach_type::did_issue;
    }
    if (output.is_did_transfer()) {
        return utxo_attach_type::did_transfer;
    }
    throw std::logic_error("get_utxo_attach_type : Unkown output type.");
}

std::string get_multisig_script(uint8_t m, uint8_t n, std::vector<std::string>& public_keys){
    std::sort(public_keys.begin(), public_keys.end());
    std::ostringstream ss;
    ss << std::to_string(m);
    for(auto& each : public_keys)
        ss << " [ " << each << " ] ";
    ss << std::to_string(n) << " checkmultisig";
    return ss.str();
}
void get_multisig_pri_pub_key(std::string& prikey, std::string& pubkey, std::string& seed, uint32_t hd_index)
{
    // TO TEST . CHENHAO
    bc::config::base16 base16_seed{seed};

    const auto& dc_seed = base16_seed;
    auto&& hd_pri_key = get_hd_new(dc_seed);
    auto&& hd_pub_key = hd_pri_key.derive_public(hd_index);

    prikey = hd_pri_key.encoded();
    pubkey = hd_pub_key.encoded();
}

history::list expand_history(history_compact::list& compact)
{
    history::list result;
    result.reserve(compact.size());

    std::unordered_map<uint64_t, history*> map_output;
    // Process and remove all outputs.
    for (auto output = compact.begin(); output != compact.end(); ++output)
    {
        if (output->kind == point_kind::output)
        {
            history row;
            row.output = output->point;
            row.output_height = output->height;
            row.value = output->value;
            row.spend = { null_hash, max_uint32 };
            row.temporary_checksum = output->point.checksum();
            result.emplace_back(row);
            map_output[row.temporary_checksum] = &result.back();
        }
    }

    //process the spends.
    for (const auto& spend: compact)
    {
        auto found = false;

        if (spend.kind == point_kind::output)
            continue;

        auto r = map_output.find(spend.previous_checksum);
        if(r != map_output.end() && r->second->spend.hash == null_hash)
        {
             r->second->spend = spend.point;
             r->second->spend_height = spend.height;
             found = true;
        }

        // This will only happen if the history height cutoff comes between
        // an output and its spend. In this case we return just the spend.
        if (!found)
        {
            history row;
            row.output = { null_hash, max_uint32 };
            row.output_height = max_uint64;
            row.value = max_uint64;
            row.spend = spend.point;
            row.spend_height = spend.height;
            result.emplace_back(row);
        }
    }

    compact.clear();

    // Clear all remaining checksums from unspent rows.
    for (auto& row: result)
        if (row.spend.hash == null_hash)
            row.spend_height = max_uint64;

    // TODO: sort by height and index of output, spend or both in order.
    return result;
}

history::list get_address_history(wallet::payment_address& addr, bc::blockchain::block_chain_impl& blockchain)
{
    history_compact::list cmp_history;
    history::list history_vec;
    if(blockchain.get_history(addr, 0, 0, cmp_history)) {
        
        history_vec = expand_history(cmp_history);
    }
    return history_vec;
}


void expand_history(history_compact::list& compact, history::list& result)
{
    // Process and remove all outputs.
    for (auto output = compact.begin(); output != compact.end();)
    {
        if (output->kind == point_kind::output)
        {
            history row;
            row.output = output->point;
            row.output_height = output->height;
            row.value = output->value;
            row.spend = { null_hash, max_uint32 };
            row.temporary_checksum = output->point.checksum();
            result.emplace_back(row);
            output = compact.erase(output);
            continue;
        }

        ++output;
    }

    // All outputs have been removed, process the spends.
    for (const auto& spend: compact)
    {
        auto found = false;

        // Update outputs with the corresponding spends.
        for (auto& row: result)
        {
            if (row.temporary_checksum == spend.previous_checksum &&
                row.spend.hash == null_hash)
            {
                row.spend = spend.point;
                row.spend_height = spend.height;
                found = true;
                break;
            }
        }

        // This will only happen if the history height cutoff comes between
        // an output and its spend. In this case we return just the spend.
        if (!found)
        {
            history row;
            row.output = { null_hash, max_uint32 };
            row.output_height = max_uint64;
            row.value = max_uint64;
            row.spend = spend.point;
            row.spend_height = spend.height;
            result.emplace_back(row);
        }
    }

    compact.clear();

    // Clear all remaining checksums from unspent rows.
    for (auto& row: result)
        if (row.spend.hash == null_hash)
            row.spend_height = max_uint64;
}

void get_address_history(wallet::payment_address& addr, bc::blockchain::block_chain_impl& blockchain,
    history::list& history_vec)
{
    history_compact::list cmp_history;
    if(blockchain.get_history(addr, 0, 0, cmp_history)) {
        
        expand_history(cmp_history, history_vec);
    }
}
void sync_fetch_asset_balance (std::string& addr, 
    bc::blockchain::block_chain_impl& blockchain, std::shared_ptr<std::vector<asset_detail>> sh_asset_vec)
{
    auto address = payment_address(addr);
    // history::list rows
    auto rows = get_address_history(address, blockchain);
    
    chain::transaction tx_temp;
    uint64_t tx_height;
    uint64_t height = 0;
    blockchain.get_last_height(height);

    for (auto& row: rows) {     
        // spend unconfirmed (or no spend attempted)
        if ((row.spend.hash == null_hash)
                && blockchain.get_transaction(row.output.hash, tx_temp, tx_height)) {
            auto output = tx_temp.outputs.at(row.output.index);
            if((output.is_asset_transfer() || output.is_asset_issue() || output.is_asset_secondaryissue())) {
                auto pos = std::find_if(sh_asset_vec->begin(), sh_asset_vec->end(), [&](const asset_detail& elem){
                        return output.get_asset_symbol() == elem.get_symbol();
                        });
                
                if (pos == sh_asset_vec->end()){ // new item
                    sh_asset_vec->push_back(asset_detail(output.get_asset_symbol(), output.get_asset_amount(), 0, 0, "", addr, ""));
                } else { // exist just add amount
                    pos->set_maximum_supply(pos->get_maximum_supply()+output.get_asset_amount());
                }
            }
        }
    
    }
}

void sync_fetch_asset_balance_record (std::string& addr, 
    bc::blockchain::block_chain_impl& blockchain, std::shared_ptr<std::vector<asset_detail>> sh_asset_vec)
{
    auto address = payment_address(addr);
    // history::list rows
    auto rows = get_address_history(address, blockchain);
    
    chain::transaction tx_temp;
    uint64_t tx_height;
    uint64_t height = 0;
    blockchain.get_last_height(height);

    for (auto& row: rows) {     
        // spend unconfirmed (or no spend attempted)
        if ((row.spend.hash == null_hash)
                && blockchain.get_transaction(row.output.hash, tx_temp, tx_height)) {
            auto output = tx_temp.outputs.at(row.output.index);
            if((output.is_asset_transfer() || output.is_asset_issue() || output.is_asset_secondaryissue())) {
                auto pos = std::find_if(sh_asset_vec->begin(), sh_asset_vec->end(), [&](const asset_detail& elem){
                        return ((output.get_asset_symbol() == elem.get_symbol()) 
                            && (addr == elem.get_address()));
                        });
                
                if (pos == sh_asset_vec->end()){ // new item
                    sh_asset_vec->push_back(asset_detail(output.get_asset_symbol(), output.get_asset_amount(), 0, 0, "", addr, ""));
                } else { // exist just add amount
                    pos->set_maximum_supply(pos->get_maximum_supply()+output.get_asset_amount());
                }
            }
        }
    
    }
}
/// amount == 0 -- get all address balances
/// amount != 0 -- get some address balances which bigger than amount 
void sync_fetchbalance (wallet::payment_address& address, 
    std::string& type, bc::blockchain::block_chain_impl& blockchain, balances& addr_balance, uint64_t amount)
{
    // history::list rows
    auto rows = get_address_history(address, blockchain);
    log::trace("get_history=")<<rows.size();

    uint64_t total_received = 0;
    uint64_t confirmed_balance = 0;
    uint64_t unspent_balance = 0;
    uint64_t frozen_balance = 0;
    
    chain::transaction tx_temp;
    uint64_t tx_height;
    uint64_t height = 0;
    blockchain.get_last_height(height);

    for (auto& row: rows)
    {
        if(amount && ((unspent_balance - frozen_balance) >= amount)) // performance improve
            break;
        
        total_received += row.value;
    
        // spend unconfirmed (or no spend attempted)
        if ((row.spend.hash == null_hash)
                && blockchain.get_transaction(row.output.hash, tx_temp, tx_height)) {
            //log::debug("get_tx=")<<blockchain.get_transaction(row.output.hash, tx_temp); // todo -- return value check
            auto output = tx_temp.outputs.at(row.output.index);

            // deposit utxo in transaction pool
            if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                        && !row.output_height) { 
                frozen_balance += row.value;
            }

            // deposit utxo in block
            if(chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
                && row.output_height) { 
                uint64_t lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
                if((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                    frozen_balance += row.value;
                }
            }
            
            // coin base etp maturity etp check
            if(tx_temp.is_coinbase()
                && !(output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)) { // incase readd deposit
                // add not coinbase_maturity etp into frozen
                if((!row.output_height ||
                            (row.output_height && (height - row.output_height) < coinbase_maturity))) {
                    frozen_balance += row.value;
                }
            }
            
            if((type == "all") 
                || ((type == "etp") && output.is_etp()))
                unspent_balance += row.value;
        }
    
        if (row.output_height != 0 &&
            (row.spend.hash == null_hash || row.spend_height == 0))
            confirmed_balance += row.value;
    }
    
    addr_balance.confirmed_balance = confirmed_balance;
    addr_balance.total_received = total_received;
    addr_balance.unspent_balance = unspent_balance;
    addr_balance.frozen_balance = frozen_balance;
    
}

code sync_fetchbalance (command& cmd, std::string& addr, 
    std::string& type, bc::blockchain::block_chain_impl& blockchain, balances& addr_balance)
{
    using namespace bc::client;

    code ec = error::success;
    auto address = payment_address(addr);
    const auto connection = get_connection(cmd);
    obelisk_client client(connection);
    if (!client.connect(connection))
    {
        throw connection_exception{"failure connection to " + connection.server.to_string()} ;
    }

    uint64_t height = 0;
    blockchain.get_last_height(height);

    auto on_done = [&addr_balance, &type, &blockchain, height](const history::list& rows)
    {       
        for (auto& row: rows) {

            addr_balance.total_received += row.value;
            //std::function<void(chain::transaction& tx, uint64_t& tx_height)>
            auto sum_balance = [&row, &addr_balance, &type, height](const code& ec, const chain::transaction& tx)-> void
            {
                auto output = tx.outputs.at(row.output.index);
                // deposit utxo in transaction pool
                if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                            && !row.output_height) { 
                    addr_balance.frozen_balance += row.value;
                }
                
                // deposit utxo in block
                if(chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
                    && row.output_height) { 
                    uint64_t lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
                    if((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                        addr_balance.frozen_balance += row.value;
                    }
                }
                
                // coin base etp maturity etp check
                if(tx.is_coinbase()
                    && !(output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)) { // incase readd deposit
                    // add not coinbase_maturity etp into frozen
                    if((!row.output_height ||
                                (row.output_height && (height - row.output_height) < coinbase_maturity))) {
                        addr_balance.frozen_balance += row.value;
                    }
                }
                
                if((type == "all") 
                    || ((type == "etp") && output.is_etp()))
                    addr_balance.unspent_balance += row.value;

            };
            // spend unconfirmed (or no spend attempted)
            if (row.spend.hash == null_hash)
                    blockchain.get_transaction_callback(row.output.hash, sum_balance); 
            
            if (row.output_height != 0 &&
                (row.spend.hash == null_hash || row.spend_height == 0))
                addr_balance.confirmed_balance += row.value;
        }
    };

    auto on_error = [&ec](const code& error)
    {
        ec = error;
    };

    // The v3 client API works with and normalizes either server API.
    //// client.address_fetch_history(on_error, on_done, address);
    client.address_fetch_history2(on_error, on_done, address);
    client.wait();

    return ec;
}
void base_transfer_helper::sum_payment_amount(){
    if(receiver_list_.empty())
        throw toaddress_empty_exception{"empty target address"};
    if (payment_etp_ > maximum_fee || payment_etp_ < minimum_fee)
        throw asset_exchange_poundage_exception{"fee must in [10000, 10000000000]"};

    for (auto& iter : receiver_list_) {
        payment_etp_ += iter.amount;
        payment_asset_ += iter.asset_amount;
    }
}
void base_transfer_helper::sync_fetchutxo (const std::string& prikey, const std::string& addr) 
{
    auto waddr = wallet::payment_address(addr);
    // history::list rows
    auto rows = get_address_history(waddr, blockchain_);
    log::trace("get_history=")<<rows.size();
        
    chain::transaction tx_temp;
    uint64_t tx_height;
    uint64_t height = 0;
    auto frozen_flag = false;
    address_asset_record record;
    
    blockchain_.get_last_height(height);

    for (auto& row: rows)
    {
        frozen_flag = false;
        if((unspent_etp_ >= payment_etp_) && (unspent_asset_ >= payment_asset_)) // performance improve
            break;
                
        // spend unconfirmed (or no spend attempted)
        if ((row.spend.hash == null_hash)
                && blockchain_.get_transaction(row.output.hash, tx_temp, tx_height)) {
            auto output = tx_temp.outputs.at(row.output.index);

            // deposit utxo in transaction pool
            if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                        && !row.output_height) { 
                frozen_flag = true;
            }

            // deposit utxo in block
            if(chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
                && row.output_height) { 
                uint64_t lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
                if((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                    frozen_flag = true;
                }
            }
            
            // coin base etp maturity etp check
            if(tx_temp.is_coinbase()
                && !(output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)) { // incase readd deposit
                // add not coinbase_maturity etp into frozen
                if((!row.output_height ||
                            (row.output_height && (height - row.output_height) < coinbase_maturity))) {
                    frozen_flag = true;
                }
            }
            log::trace("frozen_flag=")<< frozen_flag;
            log::trace("payment_asset_=")<< payment_asset_;
            log::trace("is_etp=")<< output.is_etp();
            log::trace("value=")<< row.value;
            log::trace("is_trans=")<< output.is_asset_transfer();
            log::trace("is_issue=")<< output.is_asset_issue();
            log::trace("symbol=")<< symbol_;
            log::trace("outpuy symbol=")<< output.get_asset_symbol();
            // add to from list
            if(!frozen_flag){
                // etp -> etp tx
                if(!payment_asset_ && output.is_etp()){
                    record.prikey = prikey;
                    record.addr = addr;
                    record.amount = row.value;
                    record.symbol = "";
                    record.asset_amount = 0;
                    record.type = utxo_attach_type::etp;
                    record.output = row.output;
                    record.script = output.script;
                    
                    if(unspent_etp_ < payment_etp_) {
                        from_list_.push_back(record);
                        unspent_etp_ += record.amount;
                    }
                // asset issue/transfer /secondary issue
                } else { 
                    if(output.is_etp()){
                        record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = "";
                        record.asset_amount = 0;
                        record.type = utxo_attach_type::etp;
                        record.output = row.output;
                        record.script = output.script;
                        
                        if(unspent_etp_ < payment_etp_) {
                            from_list_.push_back(record);
                            unspent_etp_ += record.amount;
                        }
                    } else if ((output.is_asset_issue() || output.is_asset_secondaryissue()) && (symbol_ == output.get_asset_symbol())){
                        record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = output.get_asset_symbol();
                        record.asset_amount = output.get_asset_amount();
                        record.type = utxo_attach_type::asset_issue;
                        record.output = row.output;
                        record.script = output.script;
                        
                        if((unspent_asset_ < payment_asset_)
                            || (unspent_etp_ < payment_etp_)) {
                            from_list_.push_back(record);
                            unspent_asset_ += record.asset_amount;
                            unspent_etp_ += record.amount;
                        }
                    } else if (output.is_asset_transfer() && (symbol_ == output.get_asset_symbol())){
                        record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = output.get_asset_symbol();
                        record.asset_amount = output.get_asset_amount();
                        record.type = utxo_attach_type::asset_transfer;
                        record.output = row.output;
                        record.script = output.script;
                        
                        if((unspent_asset_ < payment_asset_)
                            || (unspent_etp_ < payment_etp_)){
                            from_list_.push_back(record);
                            unspent_asset_ += record.asset_amount;
                            unspent_etp_ += record.amount;
                        }
                        log::trace("unspent_asset_=")<< unspent_asset_;
                        log::trace("unspent_etp_=")<< unspent_etp_;
                    }else if (output.is_did_issue() && (symbol_ == output.get_did_symbol())){
                        record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = output.get_did_symbol();
                        record.asset_amount = 0;
                        record.type = utxo_attach_type::did_issue;
                        record.output = row.output;
                        record.script = output.script;
                        
                        if((unspent_asset_ < payment_asset_)
                            || (unspent_etp_ < payment_etp_)) {
                            from_list_.push_back(record);
                            unspent_asset_ += record.asset_amount;
                            unspent_etp_ += record.amount;
                        }
                    }else if (output.is_did_transfer() && (symbol_ == output.get_did_symbol())){
                        record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = output.get_did_symbol();
                        record.asset_amount = 0;
                        record.type = utxo_attach_type::did_transfer;
                        record.output = row.output;
                        record.script = output.script;
                        
                        if(unspent_etp_ < payment_etp_) {
                            from_list_.push_back(record);
                            unspent_etp_ += record.amount;
                        }
                        log::trace("unspent_etp_=")<< unspent_etp_;
                    }
                    // not add message process here, because message utxo have no etp value
                }
            }
        }
    
    }
    rows.clear();
    
}

void base_transfer_helper::populate_unspent_list() {
    // get address list
    auto pvaddr = blockchain_.get_account_addresses(name_);
    if(!pvaddr) 
        throw address_list_nullptr_exception{"nullptr for address list"};

    // get from address balances
    for (auto& each : *pvaddr){
        // filter script address
        if(blockchain_.is_script_address(each.get_address()))
            continue;
        if(from_.empty()) { // select utxo in all account addresses
            sync_fetchutxo (each.get_prv_key(passwd_), each.get_address());
            if((unspent_etp_ >= payment_etp_)
                && (unspent_asset_ >= payment_asset_))
                break;
        } else { // select utxo only in from_ address
            if ( from_ == each.get_address() ) { // find address
                sync_fetchutxo (each.get_prv_key(passwd_), each.get_address());
                if((unspent_etp_ >= payment_etp_)
                    && (unspent_asset_ >= payment_asset_))
                    break;
            }
        }
    }
    
    if(from_list_.empty())
        throw tx_source_exception{"not enough etp in from address or you are't own from address!"};

    // addresses balances check
    if(unspent_etp_ < payment_etp_)
        throw account_balance_lack_exception{"no enough balance"};
    if(unspent_asset_ < payment_asset_)
        throw asset_lack_exception{"no enough asset amount"};
    if (!asset_cert::test_certs(unspent_asset_cert_, payment_asset_cert_))
        throw asset_cert_exception{"no enough asset cert"};

    // change
    populate_change();
}

void base_transfer_helper::populate_tx_inputs(){
    // input args
    uint64_t adjust_amount = 0;
    tx_input_type input;

    for (auto& fromeach : from_list_){
        adjust_amount += fromeach.amount;
        if (tx_item_idx_ >= tx_limit) // limit in ~333 inputs
        {
            auto&& response = "Too many inputs limit, suggest less than " + std::to_string(adjust_amount) + " satoshi.";
            throw std::runtime_error(response);
        }
        tx_item_idx_++;
        input.sequence = max_input_sequence;
        input.previous_output.hash = fromeach.output.hash;
        input.previous_output.index = fromeach.output.index;
        tx_.inputs.push_back(input);
    }
}
attachment base_transfer_helper::populate_output_attachment(receiver_record& record){
            
    if((record.type == utxo_attach_type::etp)
        || (record.type == utxo_attach_type::deposit)
        || (((record.type == utxo_attach_type::asset_transfer) || (record.type == utxo_attach_type::asset_locked_transfer)) 
                && ((record.amount > 0) && (!record.asset_amount)))) { // etp
        return attachment(ETP_TYPE, attach_version, libbitcoin::chain::etp(record.amount));
    } 

    if(record.type == utxo_attach_type::asset_issue) {
        auto sh_asset = blockchain_.get_account_unissued_asset(name_, symbol_);
        if(!sh_asset)
            throw asset_symbol_notfound_exception{symbol_ + " not found"};
        
        sh_asset->set_address(record.target); // target is setted in metaverse_output.cpp
        auto ass = asset(ASSET_DETAIL_TYPE, *sh_asset);
        return attachment(ASSET_TYPE, attach_version, ass);
    } else if(record.type == utxo_attach_type::asset_secondaryissue) {
        auto sh_asset = blockchain_.get_account_unissued_asset(name_, symbol_);
        if(!sh_asset)
            throw asset_symbol_notfound_exception{symbol_ + " not found"};
        
        sh_asset->set_address(record.target); // target is setted in metaverse_output.cpp
        auto ass = asset(ASSET_DETAIL_TYPE, *sh_asset);
        return attachment(ASSET_TYPE, attach_version, ass);
    } else if(record.type == utxo_attach_type::asset_transfer) {
        auto transfer = libbitcoin::chain::asset_transfer(record.symbol, record.asset_amount);
        auto ass = asset(ASSET_TRANSFERABLE_TYPE, transfer);
        return attachment(ASSET_TYPE, attach_version, ass);
    } else if(record.type == utxo_attach_type::message) {
        auto msg = boost::get<bc::chain::blockchain_message>(record.attach_elem.get_attach());
        return attachment(MESSAGE_TYPE, attach_version, msg);
    } else if(record.type == utxo_attach_type::did_issue) {
        auto sh_did = blockchain_.get_account_unissued_did(name_, symbol_);
        if(!sh_did)
            throw did_symbol_notfound_exception{symbol_ + " not found"};
     
        sh_did->set_address(record.target); // target is setted in metaverse_output.cpp
        auto ass = did(DID_DETAIL_TYPE, *sh_did);
        return attachment(DID_TYPE, attach_version, ass);
    } else if(record.type == utxo_attach_type::did_transfer) {
        auto sh_did = blockchain_.get_issued_did(symbol_);
        if(!sh_did)
            throw did_symbol_notfound_exception{symbol_ + " not found"};
     
        sh_did->set_address(record.target); // target is setted in metaverse_output.cpp
        auto ass = did(DID_TRANSFERABLE_TYPE, *sh_did);
        return attachment(DID_TYPE, attach_version, ass);
    } else if (record.type == utxo_attach_type::asset_cert) {
        if (record.asset_cert == asset_cert_ns::none) {
            throw asset_cert_exception("asset cert is none");
        }
        auto cert_owner = asset_cert::get_owner_from_address(record.target, blockchain_);
        auto cert_info = chain::asset_cert(symbol_, cert_owner, record.asset_cert);
        return attachment(ASSET_CERT_TYPE, attach_version, cert_info);
    }

    throw tx_attachment_value_exception{"invalid attachment value in receiver_record"};
}

void base_transfer_helper::populate_tx_outputs(){
    chain::operation::stack payment_ops;
    
    for (auto& iter: receiver_list_) {
        if (tx_item_idx_ >= (tx_limit + 10)) {
                throw std::runtime_error{"Too many inputs/outputs makes tx too large, canceled."};
        }
        tx_item_idx_++;
        
        // filter zero etp and asset. status check just for issue asset
        #if 0
        if( !iter.amount && !iter.asset_amount && (iter.status != asset::asset_status::asset_locked))
            continue;
        #endif
        if( !iter.amount && ((iter.type == utxo_attach_type::etp) || (iter.type == utxo_attach_type::deposit))) // etp business , value == 0
            continue;
        if( !iter.amount && !iter.asset_amount 
            && ((iter.type == utxo_attach_type::asset_transfer)|| (iter.type == utxo_attach_type::asset_locked_transfer))) // asset transfer business, etp == 0 && asset_amount == 0
            continue;
        
        // complicated script and asset should be implemented in subclass
        // generate script          
        const wallet::payment_address payment(iter.target);
        if (!payment)
            throw toaddress_invalid_exception{"invalid target address"};

        auto&& hash = payment.hash();
        if (payment.version() == wallet::payment_address::mainnet_p2kh) 
        {
            payment_ops = chain::operation::to_pay_key_hash_pattern(hash); 
        } 
        else if (payment.version() == wallet::payment_address::mainnet_p2sh) 
        { 
            payment_ops = chain::operation::to_pay_script_hash_pattern(hash); 
        }

        auto payment_script = chain::script{ payment_ops };
        
        // generate asset info
        auto output_att = populate_output_attachment(iter);

        if (!output_att.is_valid()) {
            throw tx_validate_exception{"validate transaction failure, invalid output attachment."};
        }

        // fill output
        tx_.outputs.push_back({ iter.amount, payment_script, output_att });
    }
}

void base_transfer_helper::check_tx(){
    if (tx_.is_locktime_conflict())
    {
        throw tx_locktime_exception{"The specified lock time is ineffective because all sequences are set to the maximum value."};
    }
    if (tx_.inputs.empty()) {
        throw tx_validate_exception{"validate transaction failure, empty inputs."};
    }
    if (tx_.outputs.empty()) {
        throw tx_validate_exception{"validate transaction failure, empty outputs."};
    }
}

void base_transfer_helper::sign_tx_inputs(){
    uint32_t index = 0;
    for (auto& fromeach : from_list_){
        // paramaters
        explorer::config::hashtype sign_type;
        uint8_t hash_type = (signature_hash_algorithm)sign_type;

        bc::explorer::config::ec_private config_private_key(fromeach.prikey);
        const ec_secret& private_key =    config_private_key;    
        bc::wallet::ec_private ec_private_key(private_key, 0u, true);

        bc::explorer::config::script config_contract(fromeach.script);
        const bc::chain::script& contract = config_contract;

        // gen sign
        bc::endorsement endorse;
        if (!bc::chain::script::create_endorsement(endorse, private_key,
            contract, tx_, index, hash_type))
        {
            throw tx_sign_exception{"get_input_sign sign failure"};
        }

        // do script
        auto&& public_key = ec_private_key.to_public();
        data_chunk public_key_data;
        public_key.to_data(public_key_data);
        bc::chain::script ss;
        ss.operations.push_back({bc::chain::opcode::special, endorse});
        ss.operations.push_back({bc::chain::opcode::special, public_key_data});
        
        // if pre-output script is deposit tx.
        if (contract.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height) {
            uint64_t lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(
                contract.operations);
            ss.operations.push_back({bc::chain::opcode::special, satoshi_to_chunk(lock_height)});
        }
        // set input script of this tx
        tx_.inputs[index].script = ss;
        index++;
    }

}

void base_transfer_helper::send_tx(){
    if(blockchain_.validate_transaction(tx_)) {
#ifdef MVS_DEBUG
        throw tx_validate_exception{"validate transaction failure. " + tx_.to_string(1)};
#endif
        throw tx_validate_exception{"validate transaction failure"};
    }
    if(blockchain_.broadcast_transaction(tx_)) 
        throw tx_broadcast_exception{"broadcast transaction failure"};
}
void base_transfer_helper::exec(){  
    // prepare 
    sum_payment_amount();
    populate_unspent_list();
    // construct tx
    populate_tx_header();
    populate_tx_inputs();
    populate_tx_outputs();
    // check tx
    check_tx();
    // sign tx
    sign_tx_inputs();
    // send tx
    send_tx();
}
tx_type& base_transfer_helper::get_transaction(){
    return tx_;
}

// copy from src/lib/consensus/clone/script/script.h
std::vector<unsigned char> base_transfer_helper::satoshi_to_chunk(const int64_t& value)
{
    if(value == 0)
        return std::vector<unsigned char>();

    std::vector<unsigned char> result;
    const bool neg = value < 0;
    uint64_t absvalue = neg ? -value : value;

    while(absvalue)
    {
        result.push_back(absvalue & 0xff);
        absvalue >>= 8;
    }

    if (result.back() & 0x80)
        result.push_back(neg ? 0x80 : 0);
    else if (neg)
        result.back() |= 0x80;

    return result;
}

void base_transaction_constructor::sum_payment_amount() {
    if(from_vec_.empty())
        throw fromaddress_empty_exception{"empty from address"};
    if(receiver_list_.empty())
        throw toaddress_empty_exception{"empty target address"};
    if (payment_etp_ > maximum_fee || payment_etp_ < minimum_fee)
        throw asset_exchange_poundage_exception{"fee must in [10000, 10000000000]"};
    
    for( auto& each : receiver_list_){
        // sum etp and asset amount
        payment_etp_ += each.amount;
        payment_asset_ += each.asset_amount;
    }

}
void base_transaction_constructor::sync_fetchutxo (const std::string& addr) 
{
    auto waddr = wallet::payment_address(addr);
    // history::list rows
    auto rows = get_address_history(waddr, blockchain_);
    log::trace("get_history=")<<rows.size();
        
    chain::transaction tx_temp;
    uint64_t tx_height;
    uint64_t height = 0;
    auto frozen_flag = false;
    address_asset_record record;
    
    blockchain_.get_last_height(height);

    for (auto& row: rows)
    {
        frozen_flag = false;
        if((unspent_etp_ >= payment_etp_) && (unspent_asset_ >= payment_asset_)) // performance improve
            break;
                
        // spend unconfirmed (or no spend attempted)
        if ((row.spend.hash == null_hash)
                && blockchain_.get_transaction(row.output.hash, tx_temp, tx_height)) {
            auto output = tx_temp.outputs.at(row.output.index);

            // deposit utxo in transaction pool
            if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                        && !row.output_height) { 
                frozen_flag = true;
            }

            // deposit utxo in block
            if(chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
                && row.output_height) { 
                uint64_t lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
                if((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                    frozen_flag = true;
                }
            }
            
            // coin base etp maturity etp check
            if(tx_temp.is_coinbase()
                && !(output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)) { // incase readd deposit
                // add not coinbase_maturity etp into frozen
                if((!row.output_height ||
                            (row.output_height && (height - row.output_height) < coinbase_maturity))) {
                    frozen_flag = true;
                }
            }
            log::trace("frozen_flag=")<< frozen_flag;
            log::trace("payment_asset_=")<< payment_asset_;
            log::trace("is_etp=")<< output.is_etp();
            log::trace("value=")<< row.value;
            log::trace("is_trans=")<< output.is_asset_transfer();
            log::trace("is_issue=")<< output.is_asset_issue();
            log::trace("symbol=")<< symbol_;
            log::trace("outpuy symbol=")<< output.get_asset_symbol();
            // add to from list
            if(!frozen_flag){
                // etp -> etp tx
                if(!payment_asset_ && output.is_etp()){
                    //record.prikey = prikey;
                    record.addr = addr;
                    record.amount = row.value;
                    record.symbol = "";
                    record.asset_amount = 0;
                    record.type = utxo_attach_type::etp;
                    record.output = row.output;
                    //record.script = output.script;
                    
                    if(unspent_etp_ < payment_etp_) {
                        from_list_.push_back(record);
                        unspent_etp_ += record.amount;
                    }
                // asset issue/transfer
                } else { 
                    if(output.is_etp()){
                        //record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = "";
                        record.asset_amount = 0;
                        record.type = utxo_attach_type::etp;
                        record.output = row.output;
                        //record.script = output.script;
                        
                        if(unspent_etp_ < payment_etp_) {
                            from_list_.push_back(record);
                            unspent_etp_ += record.amount;
                        }
                    } else if (output.is_asset_issue() && (symbol_ == output.get_asset_symbol())){
                        //record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = output.get_asset_symbol();
                        record.asset_amount = output.get_asset_amount();
                        record.type = utxo_attach_type::asset_issue;
                        record.output = row.output;
                        //record.script = output.script;
                        
                        if((unspent_asset_ < payment_asset_)
                            || (unspent_etp_ < payment_etp_)) {
                            from_list_.push_back(record);
                            unspent_asset_ += record.asset_amount;
                            unspent_etp_ += record.amount;
                        }
                    } else if (output.is_asset_transfer() && (symbol_ == output.get_asset_symbol())){
                        //record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = output.get_asset_symbol();
                        record.asset_amount = output.get_asset_amount();
                        record.type = utxo_attach_type::asset_transfer;
                        record.output = row.output;
                        //record.script = output.script;
                        
                        if((unspent_asset_ < payment_asset_)
                            || (unspent_etp_ < payment_etp_)){
                            from_list_.push_back(record);
                            unspent_asset_ += record.asset_amount;
                            unspent_etp_ += record.amount;
                        }
                        log::trace("unspent_asset_=")<< unspent_asset_;
                        log::trace("unspent_etp_=")<< unspent_etp_;
                    } else if (output.is_did_issue() && (symbol_ == output.get_did_symbol())){
                        //record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = output.get_did_symbol();
                        record.asset_amount = 0;
                        record.type = utxo_attach_type::did_issue;
                        record.output = row.output;
                        //record.script = output.script;
                        
                        if(unspent_etp_ < payment_etp_) {
                            from_list_.push_back(record);
                            unspent_etp_ += record.amount;
                        }
                    } else if (output.is_did_transfer() && (symbol_ == output.get_did_symbol())){
                        //record.prikey = prikey;
                        record.addr = addr;
                        record.amount = row.value;
                        record.symbol = output.get_did_symbol();
                        record.asset_amount = 0;
                        record.type = utxo_attach_type::did_transfer;
                        record.output = row.output;
                        //record.script = output.script;
                        
                        if(unspent_etp_ < payment_etp_) {
                            from_list_.push_back(record);
                            unspent_etp_ += record.amount;
                        }
                    } 
                    // not add message process here, because message utxo have no etp value
                }
            }
        }
    
    }
    rows.clear();
    
}

void base_transaction_constructor::populate_change() {
    if(mychange_.empty()) 
        mychange_ = from_list_.at(0).addr;

    if(unspent_etp_ - payment_etp_)
        receiver_list_.push_back({mychange_, "", unspent_etp_ - payment_etp_, 0,
    utxo_attach_type::etp, attachment()});
    
    if(unspent_asset_ - payment_asset_)
        receiver_list_.push_back({mychange_, symbol_, 0, unspent_asset_ - payment_asset_,
    utxo_attach_type::asset_transfer, attachment()});

    if(!message_.empty()) // etp transfer/asset transfer  -- with message
        receiver_list_.push_back({mychange_, "", 0, 0, 
        utxo_attach_type::message, attachment()}); 
}

void base_transaction_constructor::populate_unspent_list() {
    // get from address balances
    for (auto& each : from_vec_) {
        base_transaction_constructor::sync_fetchutxo (each);
        if((unspent_etp_ >= payment_etp_)
            && (unspent_asset_ >= payment_asset_))
            break;
    }
    
    if(from_list_.empty())
        throw tx_source_exception{"not enough etp or asset in from address!"};

    // addresses balances check
    if(unspent_etp_ < payment_etp_)
        throw account_balance_lack_exception{"no enough balance"};
    if(unspent_asset_ < payment_asset_)
        throw asset_lack_exception{"no enough asset amount"};

    // change
    populate_change();
}

void base_transaction_constructor::populate_tx_inputs(){
    // input args
    uint64_t adjust_amount = 0;
    tx_input_type input;

    for (auto& fromeach : from_list_){
        adjust_amount += fromeach.amount;
        if (tx_item_idx_ >= tx_limit) // limit in ~333 inputs
        {
            auto&& response = "Too many inputs limit, suggest less than " + std::to_string(adjust_amount) + " satoshi.";
            throw std::runtime_error(response);
        }
        tx_item_idx_++;
        input.sequence = max_input_sequence;
        input.previous_output.hash = fromeach.output.hash;
        input.previous_output.index = fromeach.output.index;
        tx_.inputs.push_back(input);
    }
}
attachment base_transaction_constructor::populate_output_attachment(receiver_record& record){
            
    if((record.type == utxo_attach_type::etp)
        || (record.type == utxo_attach_type::deposit)
        || (((record.type == utxo_attach_type::asset_transfer) || (record.type == utxo_attach_type::asset_locked_transfer)) 
                && ((record.amount > 0) && (!record.asset_amount)))) { // etp
        return attachment(ETP_TYPE, attach_version, libbitcoin::chain::etp(record.amount));
    } 

    if(record.type == utxo_attach_type::asset_issue) {
        throw tx_attachment_value_exception{"not support this utxo type"};
    } else if(record.type == utxo_attach_type::asset_transfer) {
        auto transfer = libbitcoin::chain::asset_transfer(record.symbol, record.asset_amount);
        auto ass = asset(ASSET_TRANSFERABLE_TYPE, transfer);
        return attachment(ASSET_TYPE, attach_version, ass);
    } else if(record.type == utxo_attach_type::message) {
        return attachment(MESSAGE_TYPE, attach_version, bc::chain::blockchain_message(message_));
    }

    throw tx_attachment_value_exception{"invalid attachment value in receiver_record"};
}

void base_transaction_constructor::populate_tx_outputs(){
    chain::operation::stack payment_ops;
    
    for (auto& iter: receiver_list_) {
        if (tx_item_idx_ >= (tx_limit + 10)) {
                throw std::runtime_error{"Too many inputs/outputs makes tx too large, canceled."};
        }
        tx_item_idx_++;
        
        // filter zero etp and asset. status check just for issue asset
        if( !iter.amount && ((iter.type == utxo_attach_type::etp) || (iter.type == utxo_attach_type::deposit))) // etp business , value == 0
            continue;
        if( !iter.amount && !iter.asset_amount 
            && ((iter.type == utxo_attach_type::asset_transfer)|| (iter.type == utxo_attach_type::asset_locked_transfer))) // asset transfer business, etp == 0 && asset_amount == 0
            continue;
        
        // complicated script and asset should be implemented in subclass
        // generate script          
        const wallet::payment_address payment(iter.target);
        if (!payment)
            throw toaddress_invalid_exception{"invalid target address"};

        auto&& hash = payment.hash();
        if (payment.version() == wallet::payment_address::mainnet_p2kh) {
            payment_ops = chain::operation::to_pay_key_hash_pattern(hash); 

        } else if(payment.version() == wallet::payment_address::mainnet_p2sh) { 
            payment_ops = chain::operation::to_pay_script_hash_pattern(hash); 

        } else {
            throw toaddress_unrecognized_exception{std::string("unrecognized target address.") + payment.encoded()};
        }

        auto&& payment_script = chain::script{ payment_ops };
        
        // generate asset info
        auto&& output_att = populate_output_attachment(iter);
        
        // fill output
        tx_.outputs.push_back({ iter.amount, payment_script, output_att });
    }
}

void base_transaction_constructor::check_tx(){
    if (tx_.is_locktime_conflict())
    {
        throw tx_locktime_exception{"The specified lock time is ineffective because all sequences are set to the maximum value."};
    }
}

void base_transaction_constructor::exec(){  
    // prepare 
    sum_payment_amount();
    populate_unspent_list();
    // construct tx
    populate_tx_header();
    populate_tx_inputs();
    populate_tx_outputs();
    // check tx
    check_tx();
}
tx_type& base_transaction_constructor::get_transaction(){
    return tx_;
}

const std::vector<uint16_t> depositing_etp::vec_cycle{7, 30, 90, 182, 365};

void depositing_etp::populate_change() {
    if(unspent_etp_ - payment_etp_) { // etp change value != 0
        if(from_.empty())
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
        else
            receiver_list_.push_back({from_, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
}

uint32_t depositing_etp::get_reward_lock_height() {
    int index = 0;
    auto it = std::find(vec_cycle.begin(), vec_cycle.end(), deposit_cycle_);
    if (it != vec_cycle.end()) { // found cycle
        index = std::distance(vec_cycle.begin(), it);
    }

    return (uint32_t)bc::consensus::lock_heights[index];
}
// modify lock script
void depositing_etp::populate_tx_outputs() {
    chain::operation::stack payment_ops;
    
    for (auto& iter: receiver_list_) {
        if (tx_item_idx_ >= (tx_limit + 10)) {
                throw std::runtime_error{"Too many inputs/outputs makes tx too large, canceled."};
        }
        tx_item_idx_++;
        
        // filter zero etp and asset
        if( !iter.amount && ((iter.type == utxo_attach_type::etp) || (iter.type == utxo_attach_type::deposit))) // etp business , value == 0
            continue;
        if( !iter.amount && !iter.asset_amount 
            && ((iter.type == utxo_attach_type::asset_transfer) || (iter.type == utxo_attach_type::asset_locked_transfer))) // asset transfer business, etp == 0 && asset_amount == 0
            continue;
        
        // complicated script and asset should be implemented in subclass
        // generate script          
        const wallet::payment_address payment(iter.target);
        if (!payment)
            throw toaddress_invalid_exception{"invalid target address"};
        auto hash = payment.hash();
        if((to_ == iter.target)
            && (utxo_attach_type::deposit == iter.type)) {
            payment_ops = chain::operation::to_pay_key_hash_with_lock_height_pattern(hash, get_reward_lock_height());
        } else {
            payment_ops = chain::operation::to_pay_key_hash_pattern(hash); // common payment script
        }
            
        auto payment_script = chain::script{ payment_ops };
        
        // generate asset info
        auto output_att = populate_output_attachment(iter);
        
        // fill output
        tx_.outputs.push_back({ iter.amount, payment_script, output_att });
    }
}

const std::vector<uint16_t> depositing_etp_transaction::vec_cycle{7, 30, 90, 182, 365};

uint32_t depositing_etp_transaction::get_reward_lock_height() {
    int index = 0;
    auto it = std::find(vec_cycle.begin(), vec_cycle.end(), deposit_);
    if (it != vec_cycle.end()) { // found cycle
        index = std::distance(vec_cycle.begin(), it);
    }

    return (uint32_t)bc::consensus::lock_heights[index];
}
// modify lock script
void depositing_etp_transaction::populate_tx_outputs() {
    chain::operation::stack payment_ops;
    
    for (auto& iter: receiver_list_) {
        if (tx_item_idx_ >= (tx_limit + 10)) {
                throw std::runtime_error{"Too many inputs/outputs makes tx too large, canceled."};
        }
        tx_item_idx_++;
        
        // filter zero etp and asset
        if( !iter.amount && ((iter.type == utxo_attach_type::etp) || (iter.type == utxo_attach_type::deposit))) // etp business , value == 0
            continue;
        if( !iter.amount && !iter.asset_amount 
            && ((iter.type == utxo_attach_type::asset_transfer) || (iter.type == utxo_attach_type::asset_locked_transfer))) // asset transfer business, etp == 0 && asset_amount == 0
            continue;
        
        // complicated script and asset should be implemented in subclass
        // generate script          
        const wallet::payment_address payment(iter.target);
        if (!payment)
            throw toaddress_invalid_exception{"invalid target address"};
        
        if (payment.version() == wallet::payment_address::mainnet_p2kh) {
            auto&& hash = payment.hash();
            if((utxo_attach_type::deposit == iter.type)) {
                payment_ops = chain::operation::to_pay_key_hash_with_lock_height_pattern(hash, get_reward_lock_height());
            } else {
                payment_ops = chain::operation::to_pay_key_hash_pattern(hash); // common payment script
            }
        } else {
            throw toaddress_invalid_exception{std::string("not supported version target address ") + iter.target};
        }  
        auto payment_script = chain::script{ payment_ops };
        
        // generate asset info
        auto output_att = populate_output_attachment(iter);
        
        // fill output
        tx_.outputs.push_back({ iter.amount, payment_script, output_att });
    }
}

        
void sending_etp::populate_change() {
    if(unspent_etp_ - payment_etp_) {
        if(from_.empty())
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
        else
            receiver_list_.push_back({from_, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
}

void sending_etp_more::populate_change() {
    if(unspent_etp_ - payment_etp_) {
        if(mychange_address_.empty())
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
        else
            receiver_list_.push_back({mychange_address_, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
}
void sending_multisig_etp::populate_change() {
    if(unspent_etp_ - payment_etp_) {
        if(from_.empty())
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
        else
            receiver_list_.push_back({from_, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
}

void sending_multisig_etp::populate_unspent_list() {
    // get address list
    auto pvaddr = blockchain_.get_account_addresses(name_);
    if(!pvaddr) 
        throw address_list_nullptr_exception{"nullptr for address list"};

    // get from address balances
    for (auto& each : *pvaddr){
        // must be script address
        if(!blockchain_.is_script_address(each.get_address()))
            continue;
        if(from_.empty()) { // select utxo in all account addresses
            base_transfer_helper::sync_fetchutxo (each.get_prv_key(passwd_), each.get_address());
            if((unspent_etp_ >= payment_etp_)
                && (unspent_asset_ >= payment_asset_))
                break;
        } else { // select utxo only in from_ address
            if ( from_ == each.get_address() ) { // find address
                base_transfer_helper::sync_fetchutxo (each.get_prv_key(passwd_), each.get_address());
                if((unspent_etp_ >= payment_etp_)
                    && (unspent_asset_ >= payment_asset_))
                    break;
            }
        }
    }
    
    if(from_list_.empty())
        throw tx_source_exception{"not enough etp in from address or you are't own from address!"};

    // addresses balances check
    if(unspent_etp_ < payment_etp_)
        throw account_balance_lack_exception{"no enough balance"};
    if(unspent_asset_ < payment_asset_)
        throw asset_lack_exception{"no enough asset amount"};

    // change
    populate_change();
}
//#include <metaverse/bitcoin/config/base16.hpp>

void sending_multisig_etp::sign_tx_inputs() {
    uint32_t index = 0;
    std::string prikey, pubkey, multisig_script;
    
    for (auto& fromeach : from_list_){
        // populate unlock script
        multisig_script = multisig_.get_multisig_script();
        log::trace("wdy script=") << multisig_script;
        //wallet::payment_address payment("3JoocenkYHEKFunupQSgBUR5bDWioiTq5Z");
        //log::trace("wdy hash=") << libbitcoin::config::base16(payment.hash());
        // prepare sign
        explorer::config::hashtype sign_type;
        uint8_t hash_type = (signature_hash_algorithm)sign_type;
        
        bc::explorer::config::ec_private config_private_key(fromeach.prikey);
        const ec_secret& private_key =    config_private_key;    
        
        bc::explorer::config::script config_contract(multisig_script);
        const bc::chain::script& contract = config_contract;
        
        // gen sign
        bc::endorsement endorse;
        if (!bc::chain::script::create_endorsement(endorse, private_key,
            contract, tx_, index, hash_type))
        {
            throw tx_sign_exception{"get_input_sign sign failure"};
        }
        // do script
        bc::chain::script ss;
        data_chunk data;
        ss.operations.push_back({bc::chain::opcode::zero, data});
        ss.operations.push_back({bc::chain::opcode::special, endorse});
        //ss.operations.push_back({bc::chain::opcode::special, endorse2});

        chain::script script_encoded;
        script_encoded.from_string(multisig_script);
        
        ss.operations.push_back({bc::chain::opcode::pushdata1, script_encoded.to_data(false)});
        
        // set input script of this tx
        tx_.inputs[index].script = ss;
        index++;
    }

}

void sending_multisig_etp::exec(){  
    // prepare 
    sum_payment_amount();
    populate_unspent_list();
    // construct tx
    populate_tx_header();
    populate_tx_inputs();
    populate_tx_outputs();
    // check tx
    check_tx();
    // sign tx
    sign_tx_inputs();
    // send tx in signmultisigtx command
    //send_tx();
}

void issuing_asset::sum_payment_amount() {
    if(receiver_list_.empty())
        throw toaddress_empty_exception{"empty target address"};
    if (payment_etp_ < 1000000000) // 10 etp now
        throw asset_issue_poundage_exception{"fee must more than 1000000000 satoshi == 10 etp"};

    for (auto& iter : receiver_list_) {
        payment_etp_ += iter.amount;
        payment_asset_ += iter.asset_amount;
    }
}

void issuing_asset::populate_change() {
    auto from_addr = !from_.empty() ? from_ : from_list_.at(0).addr;
    // etp utxo
    if (unspent_etp_ > payment_etp_) {
        receiver_list_.push_back({from_addr, "",
                unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
}

void secondary_issuing_asset::sum_payment_amount() {
    if (receiver_list_.empty())
        throw std::logic_error{"empty target address"};
    if (receiver_list_.size() > 1)
        throw toaddress_invalid_exception{"multiple target address"};
    if (payment_etp_ < 10000) // 10000 ETP bits now
        throw asset_exchange_poundage_exception{"fee must more than 10000 satoshi == 0.0001 etp"};
    if (payment_etp_ > maximum_fee)
        throw asset_exchange_poundage_exception{"fee must be less than 10000000000 satoshi == 100 etp"};

    for (auto& iter : receiver_list_) {
        payment_etp_ += iter.amount;
        payment_asset_ += iter.asset_amount;
        payment_asset_cert_ |= iter.asset_cert;
    }
    if (payment_asset_cert_ != asset_cert_ns::issue)
        throw asset_cert_exception("no asset cert of issue right is provided.");
}

void secondary_issuing_asset::populate_unspent_list()
{
    if (!issued_asset_)
        issued_asset_ = blockchain_.get_issued_asset(symbol_);
    if (!issued_asset_)
        throw asset_symbol_notfound_exception{"asset symbol is not exist in blockchain"};

    // get address list
    auto pvaddr = blockchain_.get_account_addresses(name_);
    if (!pvaddr)
        throw address_list_nullptr_exception{"nullptr for address list"};

    // get from address balances
    for (const auto& each : *pvaddr) {
        // filter script address
        if (blockchain_.is_script_address(each.get_address()))
            continue;

        // select utxo in all account addresses
        sync_fetchutxo(each.get_prv_key(passwd_), each.get_address());
    }

    if (from_list_.empty())
        throw tx_source_exception{"not enough etp in from address or you are't own from address!"};

    // addresses balances check
    if (unspent_etp_ < payment_etp_)
        throw account_balance_lack_exception{"no enough balance"};

    // asset amount check
    if (unspent_asset_ == 0)
        throw asset_lack_exception{"no enough asset amount"};

    if (!asset_cert::test_certs(unspent_asset_cert_, payment_asset_cert_))
        throw asset_cert_exception{"no enough asset cert right"};

    // change
    populate_change();
}

void secondary_issuing_asset::populate_change() {
    auto from_addr = !from_.empty() ? from_ : from_list_.at(0).addr;
    // etp utxo
    if (unspent_etp_ > payment_etp_) {
        receiver_list_.push_back({from_addr, "",
                unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
    // asset utxo
    auto target_addr = receiver_list_.at(0).target;
    if (unspent_asset_ > payment_asset_) {
        receiver_list_.push_back({target_addr, symbol_,
                0, unspent_asset_ - payment_asset_, utxo_attach_type::asset_transfer, attachment()});
    }
    // asset cert utxo
    auto cert_left = unspent_asset_cert_ & (~payment_asset_cert_);
    if (cert_left != asset_cert_ns::none) {
        receiver_list_.push_back({target_addr, symbol_,
                0, cert_left, utxo_attach_type::asset_cert, attachment()});
    }
}

attachment secondary_issuing_asset::populate_output_attachment(receiver_record& record){
    if (record.type == utxo_attach_type::etp) {
        return attachment(ETP_TYPE, attach_version, libbitcoin::chain::etp(record.amount));
    } else if (record.type == utxo_attach_type::asset_transfer) {
        auto transfer = libbitcoin::chain::asset_transfer(record.symbol, record.asset_amount);
        auto ass = asset(ASSET_TRANSFERABLE_TYPE, transfer);
        return attachment(ASSET_TYPE, attach_version, ass);
    } else if (record.type == utxo_attach_type::asset_cert) {
        if (record.asset_cert == asset_cert_ns::none) {
            throw asset_cert_exception("asset cert is none");
        }
        auto cert_owner = asset_cert::get_owner_from_address(record.target, blockchain_);
        auto cert_info = chain::asset_cert(symbol_, cert_owner, record.asset_cert);
        return attachment(ASSET_CERT_TYPE, attach_version, cert_info);
    } else if (record.type == utxo_attach_type::asset_secondaryissue) {
        auto asset_detail = *issued_asset_;
        asset_detail.set_address(record.target); // target is setted in metaverse_output.cpp
        asset_detail.set_asset_secondaryissue();
        asset_detail.set_maximum_supply(volume_);
        asset_detail.set_issuer(name_);
        auto ass = asset(ASSET_DETAIL_TYPE, asset_detail);
        return attachment(ASSET_TYPE, attach_version, ass);
    }
            
    throw tx_attachment_value_exception{"invalid attachment value in receiver_record"};
}

void secondary_issuing_asset::sync_fetchutxo (const std::string& prikey, const std::string& addr)
{
    auto waddr = wallet::payment_address(addr);
    auto rows = get_address_history(waddr, blockchain_);

    auto target_addr = receiver_list_.at(0).target;

    uint64_t height = 0;
    blockchain_.get_last_height(height);

    for (auto& row: rows)
    {
        // spended
        if (row.spend.hash != null_hash)
            continue;

        chain::transaction tx_temp;
        uint64_t tx_height;
        if (!blockchain_.get_transaction(row.output.hash, tx_temp, tx_height))
            continue;

        auto output = tx_temp.outputs.at(row.output.index);
        auto asset_amount = output.get_asset_amount();
        auto asset_symbol = output.get_asset_symbol();
        auto asset_certs = output.get_asset_cert_type();
        auto etp_amount = row.value;

        // filter output
        if (output.is_etp()) {
            BITCOIN_ASSERT(asset_amount == 0);
            BITCOIN_ASSERT(asset_symbol.empty());
            if (etp_amount == 0)
                continue;
            // enough to pay tx fees, and no asset to connect
            if (unspent_etp_ >= payment_etp_)
                continue;
            // if secified from address, then ignore others
            if (!from_.empty() && (from_ != addr))
                continue;
        } else {
            if (asset_amount == 0)
                continue;
            if (symbol_ != asset_symbol)
                continue;
            if ((asset_certs & payment_asset_cert_) == asset_cert_ns::none)
                continue;
            // only get asset from specified address
            if (addr != target_addr)
                continue;
        }

        // deposit utxo in transaction pool
        if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                    && (row.output_height == 0)) {
            continue;
        } else if (tx_temp.is_coinbase()) { // incase readd deposit
            // coin base etp maturity etp check
            // coinbase_maturity etp check
            if ((row.output_height == 0) || ((row.output_height + coinbase_maturity) > height)) {
                continue;
            }
        }

        // deposit utxo in block
        if (chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
            && (row.output_height != 0)) {
            auto lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
            if ((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                continue;
            }
        }

        // add to from list
        address_asset_record record;

        record.prikey = prikey;
        record.addr = addr;
        record.amount = etp_amount;
        record.symbol = symbol_;
        record.asset_amount = asset_amount;
        record.asset_cert = asset_certs;
        record.output = row.output;
        record.script = output.script;
        record.type = get_utxo_attach_type(output);

        from_list_.push_back(record);

        unspent_etp_ += record.amount;
        unspent_asset_ += record.asset_amount;
        unspent_asset_cert_ |= record.asset_cert;
    }
    rows.clear();

    // if specified from address but no enough etp to pay fees,
    // the tx will fail.
    if ((from_ == addr) && (unspent_etp_ < payment_etp_))
        throw tx_source_exception{"not enough etp in from address!"};

    if (addr == target_addr) {
        // verify if the threshhold is satisfied
        auto total_volume = blockchain_.get_asset_volume(symbol_);
        auto threshold = issued_asset_->get_secondaryissue_threshold();
        if (!asset_detail::is_secondaryissue_owns_enough(unspent_asset_, total_volume, threshold)) {
            throw asset_lack_exception{"asset volum is not enought to secondaryissue on address " + addr + ", unspent = "
                + std::to_string(unspent_asset_) + ", total_volume = " + std::to_string(total_volume)};
        }
        // verify if asset cert right is satisfied
        if (!asset_cert::test_certs(unspent_asset_cert_, payment_asset_cert_)) {
            throw asset_cert_exception{"no enough asset cert right"};
        }
    }
}

void issuing_did::sum_payment_amount() {
    if(receiver_list_.empty())
        throw toaddress_empty_exception{"empty target address"};
    //if (payment_etp_ < maximum_fee)
    if (payment_etp_ < 100000000) // test code 10 etp now
        throw did_issue_poundage_exception{"fee must more than 100000000 satoshi == 100 etp"};

    for (auto& iter : receiver_list_) {
        payment_etp_ += iter.amount;
        payment_asset_ += iter.asset_amount;
    }
}

void issuing_did::populate_change() {
    if(from_.empty()) {
        if(unspent_etp_ - payment_etp_) // etp value != 0
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    } else {
        if(unspent_etp_ - payment_etp_) // etp value != 0
            receiver_list_.push_back({from_, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
}

void issuing_locked_asset::sum_payment_amount() {
    if(receiver_list_.empty())
        throw toaddress_empty_exception{"empty target address"};
    //if (payment_etp_ < maximum_fee)
    if (payment_etp_ < 1000000000) // test code 10 etp now
        throw asset_issue_poundage_exception{"fee must more than 1000000000 satoshi == 10 etp"};

    for (auto& iter : receiver_list_) {
        payment_etp_ += iter.amount;
        payment_asset_ += iter.asset_amount;
    }
}

void issuing_locked_asset::populate_change() {
    if(from_.empty()){
        if(unspent_etp_ - payment_etp_) 
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
        //receiver_list_.push_back({from_list_.at(0).addr, symbol_, 0, 0, utxo_attach_type::asset_locked_issue, attachment()});
    } else {
        if(unspent_etp_ - payment_etp_) 
            receiver_list_.push_back({from_, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
        //receiver_list_.push_back({from_, symbol_, 0, 0, utxo_attach_type::asset_locked_issue, attachment()});
    }
}
uint32_t issuing_locked_asset::get_lock_height(){
    //uint64_t height = (deposit_cycle_*24)*(3600/24);
    uint64_t height = (deposit_cycle_)*3600;
    if(0xffffffff <= height)
        throw tx_locktime_exception{"lock time outofbound!"};
    return static_cast<uint32_t>(height);
}
// modify lock script
void issuing_locked_asset::populate_tx_outputs() {
    chain::operation::stack payment_ops;
    
    for (auto iter = receiver_list_.begin(); iter != receiver_list_.end(); iter++) {
        if (tx_item_idx_ >= (tx_limit + 10)) {
                throw std::runtime_error{"Too many inputs/outputs makes tx too large, canceled."};
        }
        tx_item_idx_++;
        
        // filter zero etp and asset. status check just for issue asset
        if( !iter->amount && ((iter->type == utxo_attach_type::etp) || (iter->type == utxo_attach_type::deposit))) // etp business , value == 0
            continue;
        if( !iter->amount && !iter->asset_amount 
            && ((iter->type == utxo_attach_type::asset_transfer) || (iter->type == utxo_attach_type::asset_locked_transfer))) // asset transfer business, etp == 0 && asset_amount == 0
            continue;
        
        // complicated script and asset should be implemented in subclass
        // generate script          
        const wallet::payment_address payment(iter->target);
        if (!payment)
            throw toaddress_invalid_exception{"invalid target address"};
        auto hash = payment.hash();
        if(utxo_attach_type::asset_locked_issue == iter->type) { // issue locked asset record
            payment_ops = chain::operation::to_pay_key_hash_with_lock_height_pattern(hash, get_lock_height());
        } else {
            payment_ops = chain::operation::to_pay_key_hash_pattern(hash); // common payment script
        }
            
        auto payment_script = chain::script{ payment_ops };
        
        // generate asset info
        auto output_att = populate_output_attachment(*iter);
        
        // fill output
        tx_.outputs.push_back({ iter->amount, payment_script, output_att });
    }
}
void sending_asset::populate_change() {
    if(from_.empty()) {
        // etp utxo
        if(unspent_etp_ - payment_etp_)
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0,
        utxo_attach_type::etp, attachment()});
        // asset utxo
        if(unspent_asset_ - payment_asset_)
            receiver_list_.push_back({from_list_.at(0).addr, symbol_, 0, unspent_asset_ - payment_asset_,
        utxo_attach_type::asset_transfer, attachment()});
    } else {
        // etp utxo
        if(unspent_etp_ - payment_etp_)
            receiver_list_.push_back({from_, "", unspent_etp_ - payment_etp_, 0,
        utxo_attach_type::etp, attachment()});
        // asset utxo
        if(unspent_asset_ - payment_asset_)
            receiver_list_.push_back({from_, symbol_, 0, unspent_asset_ - payment_asset_,
        utxo_attach_type::asset_transfer, attachment()});
    }
}

void sending_did::populate_change() {
    if(unspent_etp_ - payment_etp_) {
        if(fromfee.empty())
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
        else
            receiver_list_.push_back({fromfee, "", unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
}

void sending_did::sync_fetchutxo (const std::string& prikey, const std::string& addr)
{
    auto waddr = wallet::payment_address(addr);
    auto rows = get_address_history(waddr, blockchain_);

    uint64_t height = 0;
    blockchain_.get_last_height(height);

    //fetch did utxo
    for (auto& row: rows)
    {
        // spended
        if (row.spend.hash != null_hash)
            continue;

        chain::transaction tx_temp;
        uint64_t tx_height;
        if (!blockchain_.get_transaction(row.output.hash, tx_temp, tx_height))
            continue;

        auto output = tx_temp.outputs.at(row.output.index);
        auto did_symbol = output.get_did_symbol();
        auto etp_amount = row.value;

        if (output.is_did_issue() || output.is_did_transfer())
        {
            if (symbol_ != did_symbol)
                continue;
        }else{
            continue;
        }

        // deposit utxo in transaction pool
        if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                    && (row.output_height == 0)) {
            continue;
        } else if (tx_temp.is_coinbase()) { // incase readd deposit
            // coin base etp maturity etp check
            // coinbase_maturity etp check
            if ((row.output_height == 0) || ((row.output_height + coinbase_maturity) > height)) {
                continue;
            }
        }

        // deposit utxo in block
        if (chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
            && (row.output_height != 0)) {
            auto lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
            if ((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                continue;
            }
        }

        // add to from list
        address_asset_record record;

        record.prikey = prikey;
        record.addr = addr;
        record.amount = etp_amount;
        record.symbol = symbol_;
        record.asset_amount = 0;
        record.output = row.output;
        record.script = output.script;
        record.type = get_utxo_attach_type(output);

        from_list_.push_back(record);

        unspent_etp_ += record.amount;
        unspent_asset_ += record.asset_amount;
    }
    rows.clear();


    waddr = wallet::payment_address(fromfee);
    rows = get_address_history(waddr, blockchain_);
    std::string feeprikey = "";

    auto pvaddr = blockchain_.get_account_addresses(name_);
    if(!pvaddr) 
        throw address_list_nullptr_exception{"nullptr for target address"};

    // get fromfee address prikey
    for (auto& each : *pvaddr) {
       
        if ( fromfee == each.get_address() ) { // find address
               feeprikey = each.get_prv_key(passwd_);
        }
        
    }

    if (feeprikey.empty()) {
        throw address_list_nullptr_exception{"nullptr get prikey for target address"};
    }

     //fetch fee utxo
    for (auto& row: rows)
    {
        // spended
        if (row.spend.hash != null_hash)
            continue;

        chain::transaction tx_temp;
        uint64_t tx_height;
        if (!blockchain_.get_transaction(row.output.hash, tx_temp, tx_height))
            continue;

        auto output = tx_temp.outputs.at(row.output.index);
        auto did_symbol = output.get_did_symbol();
        auto etp_amount = row.value;

        // filter output
        if (output.is_etp()) 
        {
            if (etp_amount == 0)
                continue;
            // enough to pay tx fees, and no asset to connect
            if (unspent_etp_ >= payment_etp_)
                continue;
            // if secified from address, then ignore others
            if (!from_.empty() && (from_ != addr))
                continue;
        } else {
            continue;
        }

        // deposit utxo in transaction pool
        if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                    && (row.output_height == 0)) {
            continue;
        } else if (tx_temp.is_coinbase()) { // incase readd deposit
            // coin base etp maturity etp check
            // coinbase_maturity etp check
            if ((row.output_height == 0) || ((row.output_height + coinbase_maturity) > height)) {
                continue;
            }
        }

        // deposit utxo in block
        if (chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
            && (row.output_height != 0)) {
            auto lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
            if ((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                continue;
            }
        }

        // add to from list
        address_asset_record record;

        record.prikey = feeprikey;
        record.addr = fromfee;
        record.amount = etp_amount;
        record.symbol = symbol_;
        record.asset_amount = 0;
        record.output = row.output;
        record.script = output.script;
        record.type = get_utxo_attach_type(output);

        from_list_.push_back(record);

        unspent_etp_ += record.amount;
        unspent_asset_ += record.asset_amount;
    }
    rows.clear();


    // if specified from address but no enough etp to pay fees,
    // the tx will fail.
    if ((from_ == addr) && (unspent_etp_ < payment_etp_))
        throw tx_source_exception{"not enough etp in to address to pay fee!"};
}

void sending_locked_asset::populate_change() {
    if(from_.empty()) {
        if(unspent_etp_ - payment_etp_)
            receiver_list_.push_back({from_list_.at(0).addr, "", unspent_etp_ - payment_etp_, 0,
        utxo_attach_type::etp, attachment()});
        
        if(unspent_asset_ - payment_asset_)
            receiver_list_.push_back({from_list_.at(0).addr, symbol_, 0, unspent_asset_ - payment_asset_,
        utxo_attach_type::asset_locked_transfer, attachment()});
    } else {
        if(unspent_etp_ - payment_etp_)
            receiver_list_.push_back({from_, "", unspent_etp_ - payment_etp_, 0,
        utxo_attach_type::etp, attachment()});

        if(unspent_asset_ - payment_asset_)
            receiver_list_.push_back({from_, symbol_, 0, unspent_asset_ - payment_asset_,
        utxo_attach_type::asset_locked_transfer, attachment()});
    }
}
uint32_t sending_locked_asset::get_lock_height(){
    //uint64_t height = (deposit_cycle_*24)*(3600/24);
    uint64_t height = (deposit_cycle_)*3600;
    if(0xffffffff <= height)
        throw tx_locktime_exception{"lock time outofbound!"};
    return static_cast<uint32_t>(height);
}
// modify lock script
void sending_locked_asset::populate_tx_outputs() {
    chain::operation::stack payment_ops;
    
    for (auto iter = receiver_list_.begin(); iter != receiver_list_.end(); iter++) {
        if (tx_item_idx_ >= (tx_limit + 10)) {
                throw std::runtime_error{"Too many inputs/outputs makes tx too large, canceled."};
        }
        tx_item_idx_++;
        
        // filter zero etp and asset. status check just for issue asset
        if( !iter->amount && ((iter->type == utxo_attach_type::etp) || (iter->type == utxo_attach_type::deposit))) // etp business , value == 0
            continue;
        if( !iter->amount && !iter->asset_amount 
            && ((iter->type == utxo_attach_type::asset_transfer) || (iter->type == utxo_attach_type::asset_locked_transfer))) // asset transfer business, etp == 0 && asset_amount == 0
            continue;
        
        // complicated script and asset should be implemented in subclass
        // generate script          
        const wallet::payment_address payment(iter->target);
        if (!payment)
            throw toaddress_invalid_exception{"invalid target address"};
        auto hash = payment.hash();
        if(utxo_attach_type::asset_locked_transfer == iter->type) {
            payment_ops = chain::operation::to_pay_key_hash_with_lock_height_pattern(hash, get_lock_height());
        } else {
            payment_ops = chain::operation::to_pay_key_hash_pattern(hash); // common payment script
        }
            
        auto payment_script = chain::script{ payment_ops };
        
        // generate asset info
        auto output_att = populate_output_attachment(*iter);
        
        // fill output
        tx_.outputs.push_back({ iter->amount, payment_script, output_att });
    }
}

void merging_asset::sum_payment_amount()
{
    if (receiver_list_.empty())
        throw toaddress_empty_exception{"empty target address"};
    if (receiver_list_.size() > 1)
        throw toaddress_invalid_exception{"multiple target address"};
    if (payment_etp_ > maximum_fee || payment_etp_ < minimum_fee)
        throw asset_exchange_poundage_exception{"fee must in [10000, 10000000000]"};
}

void merging_asset::sync_fetchutxo (const std::string& prikey, const std::string& addr)
{
    auto waddr = wallet::payment_address(addr);
    auto rows = get_address_history(waddr, blockchain_);

    uint64_t height = 0;
    blockchain_.get_last_height(height);

    for (auto& row: rows)
    {
        // spended
        if (row.spend.hash != null_hash)
            continue;

        chain::transaction tx_temp;
        uint64_t tx_height;
        if (!blockchain_.get_transaction(row.output.hash, tx_temp, tx_height))
            continue;

        auto output = tx_temp.outputs.at(row.output.index);
        auto asset_amount = output.get_asset_amount();
        auto asset_symbol = output.get_asset_symbol();
        auto etp_amount = row.value;

        // filter output
        if (output.is_etp()) {
            BITCOIN_ASSERT(asset_amount == 0);
            BITCOIN_ASSERT(asset_symbol.empty());
            if (etp_amount == 0)
                continue;
            // enough to pay tx fees, and no asset to connect
            if (unspent_etp_ >= payment_etp_)
                continue;
            // if secified from address, then ignore others
            if (!from_.empty() && (from_ != addr))
                continue;
        } else {
            if (asset_amount == 0)
                continue;
            if (symbol_ != asset_symbol)
                continue;
        }

        // deposit utxo in transaction pool
        if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                    && (row.output_height == 0)) {
            continue;
        } else if (tx_temp.is_coinbase()) { // incase readd deposit
            // coin base etp maturity etp check
            // coinbase_maturity etp check
            if ((row.output_height == 0) || ((row.output_height + coinbase_maturity) > height)) {
                continue;
            }
        }

        // deposit utxo in block
        if (chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
            && (row.output_height != 0)) {
            auto lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
            if ((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                continue;
            }
        }

        // add to from list
        address_asset_record record;

        record.prikey = prikey;
        record.addr = addr;
        record.amount = etp_amount;
        record.symbol = symbol_;
        record.asset_amount = asset_amount;
        record.output = row.output;
        record.script = output.script;
        record.type = get_utxo_attach_type(output);

        from_list_.push_back(record);

        unspent_etp_ += record.amount;
        unspent_asset_ += record.asset_amount;
    }
    rows.clear();

    // if specified from address but no enough etp to pay fees,
    // the tx will fail.
    if ((from_ == addr) && (unspent_etp_ < payment_etp_))
        throw tx_source_exception{"not enough etp in from address!"};
}

void merging_asset::populate_change()
{
    auto from_addr = !from_.empty() ? from_ : from_list_.at(0).addr;
    // etp utxo
    if (unspent_etp_ > payment_etp_) {
        receiver_list_.push_back({from_addr, "",
                unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
    // asset utxo
    auto target_addr = receiver_list_.at(0).target;
    if (unspent_asset_ > payment_asset_) {
        receiver_list_.push_back({target_addr, symbol_,
                0, unspent_asset_ - payment_asset_, utxo_attach_type::asset_transfer, attachment()});
    }
}

void merging_asset::populate_unspent_list()
{
    // get address list
    auto pvaddr = blockchain_.get_account_addresses(name_);
    if (!pvaddr)
        throw address_list_nullptr_exception{"nullptr for address list"};

    // get from address balances
    for (const auto& each : *pvaddr) {
        // filter script address
        if (blockchain_.is_script_address(each.get_address()))
            continue;

        // select utxo in all account addresses
        sync_fetchutxo(each.get_prv_key(passwd_), each.get_address());
    }

    if (from_list_.empty())
        throw tx_source_exception{"not enough etp in from address or you are't own from address!"};

    // addresses balances check
    if (unspent_etp_ < payment_etp_)
        throw account_balance_lack_exception{"no enough balance"};

    // asset amount check
    if (unspent_asset_ == 0)
        throw asset_lack_exception{"no enough asset amount"};

    // change
    populate_change();
}

void transferring_asset_cert::sum_payment_amount()
{
    if (from_.empty())
        throw fromaddress_empty_exception{"empty from address"};

    if (receiver_list_.empty())
        throw toaddress_empty_exception{"empty target address"};

    if (receiver_list_.size() > 1)
        throw toaddress_invalid_exception{"multiple target address"};

    if (payment_etp_ > maximum_fee || payment_etp_ < minimum_fee)
        throw asset_exchange_poundage_exception{"fee must in [10000, 10000000000]"};

    for (auto& iter : receiver_list_) {
        payment_etp_ += iter.amount;
        payment_asset_ += iter.asset_amount;
        payment_asset_cert_ |= iter.asset_cert;
    }
}

void transferring_asset_cert::populate_change()
{
    // etp utxo
    if (unspent_etp_ > payment_etp_) {
        receiver_list_.push_back({from_, "",
                unspent_etp_ - payment_etp_, 0, utxo_attach_type::etp, attachment()});
    }
    // asset cert utxo
    auto cert_left = unspent_asset_cert_ & (~payment_asset_cert_);
    if (cert_left != asset_cert_ns::none) {
        receiver_list_.push_back({from_, symbol_,
                0, cert_left, utxo_attach_type::asset_cert, attachment()});
    }
}

void transferring_asset_cert::sync_fetchutxo (const std::string& prikey, const std::string& addr)
{
    auto waddr = wallet::payment_address(addr);
    auto rows = get_address_history(waddr, blockchain_);

    uint64_t height = 0;
    blockchain_.get_last_height(height);

    for (auto& row: rows)
    {
        // spended
        if (row.spend.hash != null_hash)
            continue;

        chain::transaction tx_temp;
        uint64_t tx_height;
        if (!blockchain_.get_transaction(row.output.hash, tx_temp, tx_height))
            continue;

        auto output = tx_temp.outputs.at(row.output.index);
        auto asset_amount = output.get_asset_amount();
        auto asset_symbol = output.get_asset_symbol();
        auto asset_certs = output.get_asset_cert_type();
        auto etp_amount = row.value;

        // filter output
        if (output.is_etp()) {
            BITCOIN_ASSERT(asset_amount == 0);
            BITCOIN_ASSERT(asset_symbol.empty());
            if (etp_amount == 0)
                continue;
            // enough to pay tx fees, and no asset to connect
            if (unspent_etp_ >= payment_etp_)
                continue;
            // if secified from address, then ignore others
            if (!from_.empty() && (from_ != addr))
                continue;
        } else if (output.is_asset_cert()) {
            BITCOIN_ASSERT(asset_amount == 0);
            if (asset_cert::test_certs(unspent_asset_cert_, payment_asset_cert_))
                continue;
            if (symbol_ != asset_symbol)
                continue;
            if ((asset_certs & payment_asset_cert_) == asset_cert_ns::none)
                continue;
        } else {
            continue;
        }

        // deposit utxo in transaction pool
        if ((output.script.pattern() == bc::chain::script_pattern::pay_key_hash_with_lock_height)
                    && (row.output_height == 0)) {
            continue;
        } else if (tx_temp.is_coinbase()) { // incase readd deposit
            // coin base etp maturity etp check
            // coinbase_maturity etp check
            if ((row.output_height == 0) || ((row.output_height + coinbase_maturity) > height)) {
                continue;
            }
        }

        // deposit utxo in block
        if (chain::operation::is_pay_key_hash_with_lock_height_pattern(output.script.operations)
            && (row.output_height != 0)) {
            auto lock_height = chain::operation::get_lock_height_from_pay_key_hash_with_lock_height(output.script.operations);
            if ((row.output_height + lock_height) > height) { // utxo already in block but deposit not expire
                continue;
            }
        }

        // add to from list
        address_asset_record record;

        record.prikey = prikey;
        record.addr = addr;
        record.amount = etp_amount;
        record.symbol = symbol_;
        record.asset_amount = asset_amount;
        record.asset_cert = asset_certs;
        record.output = row.output;
        record.script = output.script;
        record.type = get_utxo_attach_type(output);

        from_list_.push_back(record);

        unspent_etp_ += record.amount;
        unspent_asset_ += record.asset_amount;
        unspent_asset_cert_ |= record.asset_cert;
    }
    rows.clear();

    // if specified from address but no enough etp to pay fees,
    // the tx will fail.
    if ((from_ == addr) && (unspent_etp_ < payment_etp_))
        throw tx_source_exception{"not enough etp in from address!"};
}

} //commands
} // explorer
} // libbitcoin
