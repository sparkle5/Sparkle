#include <bts/blockchain/asset_record.hpp>

namespace bts { namespace blockchain {

share_type asset_record::available_shares()const
{
    return maximum_share_supply - current_share_supply;
}

bool asset_record::can_issue( const asset& amount )const
{
    if( id != amount.asset_id ) return false;
    return can_issue( amount.amount );
}

bool asset_record::can_issue( const share_type& amount )const
{
    if( amount <= 0 ) return false;
    auto new_share_supply = current_share_supply + amount;
    // catch overflow conditions
    return (new_share_supply > current_share_supply) && (new_share_supply <= maximum_share_supply);
}

asset_record asset_record::make_null()const
{
    asset_record cpy(*this);
    cpy.issuer_account_id = -1;
    return cpy;
}

}} // bts::blockchain
