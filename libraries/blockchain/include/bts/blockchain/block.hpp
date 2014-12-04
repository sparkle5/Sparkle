#pragma once

#include <bts/blockchain/transaction.hpp>

namespace bts { namespace blockchain {

   struct block_header
   {
       digest_type   digest()const;

       block_header():block_num(0){}

       block_id_type        previous;
       uint16_t             version = 0;
       uint32_t             block_num;
       fc::time_point_sec   timestamp;
       digest_type          transaction_digest;
       vector<char>         reserved; // future expansion
   };

   struct signed_block_header : public block_header
   {
       block_id_type    id()const;
       int64_t          difficulty()const;
       address          miner()const;
       signature_type   signature;
   };

   struct digest_block : public signed_block_header
   {
       digest_block( const signed_block_header& c )
       :signed_block_header(c){}

       digest_block(){}

       bool                             validate_digest()const;
       bool                             validate_unique()const;
       digest_type                      calculate_transaction_digest()const;
       std::vector<transaction_id_type> user_transaction_ids;
   };

   struct full_block : public signed_block_header
   {
       size_t               block_size()const;
       signed_transactions  user_transactions;

       operator digest_block()const;
   };

} } // bts::blockchain

FC_REFLECT( bts::blockchain::block_header,
            (previous)(version)(block_num)(timestamp)(transaction_digest)(reserved) )
FC_REFLECT_DERIVED( bts::blockchain::signed_block_header, (bts::blockchain::block_header), (signature) )
FC_REFLECT_DERIVED( bts::blockchain::digest_block, (bts::blockchain::signed_block_header), (user_transaction_ids) )
FC_REFLECT_DERIVED( bts::blockchain::full_block, (bts::blockchain::signed_block_header), (user_transactions) )
