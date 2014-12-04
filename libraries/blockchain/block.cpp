#include <bts/blockchain/block.hpp>
#include <algorithm>

namespace bts { namespace blockchain {

   digest_type block_header::digest()const
   {
      fc::sha256::encoder enc;
      fc::raw::pack( enc, *this );
      return enc.result();
   }

   address       signed_block_header::miner()const
   {
      return address(fc::ecc::public_key(signature, digest()));
   }
   block_id_type signed_block_header::id()const
   {
      fc::sha512::encoder enc;
      fc::raw::pack( enc, *this );
      return fc::ripemd160::hash( enc.result() );
   }

   size_t full_block::block_size()const
   {
      fc::datastream<size_t> ds;
      fc::raw::pack( ds, *this );
      return ds.tellp();
   }

   digest_type digest_block::calculate_transaction_digest()const
   {
      fc::sha512::encoder enc;
      fc::raw::pack( enc, user_transaction_ids );
      return fc::sha256::hash( enc.result() );
   }

   full_block::operator digest_block()const
   {
      digest_block db( (signed_block_header&)*this );
      db.user_transaction_ids.reserve( user_transactions.size() );
      for( auto item : user_transactions )
         db.user_transaction_ids.push_back( item.id() );
      return db;
   }

   bool digest_block::validate_digest()const
   {
      return calculate_transaction_digest() == transaction_digest;
   }

   bool digest_block::validate_unique()const
   {
      std::unordered_set<transaction_id_type> trx_ids;
      for( auto id : user_transaction_ids )
         if( !trx_ids.insert(id).second ) return false;
      return true;
   }

   int64_t signed_block_header::difficulty()const
   {
       const fc::sha256 max_hash;
       memset( max_hash.data(), 0xff, sizeof(max_hash) );
       max_hash.data()[0] = 0x0f;
       max_hash.data()[1] = 0xff;
       fc::bigint max_hash_int( max_hash.data(), sizeof(max_hash) );
       const auto block_id = id();
       fc::bigint block_id_int( block_id.data(), sizeof(max_hash) );

       auto block_difficulty = max_hash_int / block_id_int;
       return block_difficulty.to_int64();
   }

} } // bts::blockchain
