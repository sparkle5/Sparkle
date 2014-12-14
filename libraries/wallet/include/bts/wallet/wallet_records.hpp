/* NOTE: Avoid renaming any record members because there will be no way to
 * unserialize existing downstream wallets */

#pragma once

#include <bts/blockchain/account_record.hpp>
#include <bts/blockchain/balance_record.hpp>
#include <bts/blockchain/extended_address.hpp>
#include <bts/blockchain/transaction.hpp>
#include <bts/blockchain/types.hpp>

#include <fc/io/raw_variant.hpp>
#include <fc/optional.hpp>
#include <fc/reflect/variant.hpp>

namespace bts { namespace wallet {
   using namespace bts::blockchain;

   /**
    *  The vote selection method helps enhance user privacy by
    *  not tying their accounts together.
    */
   enum vote_selection_method
   {
      vote_none        = 0,
      vote_all         = 1,
      vote_random      = 2,
      vote_recommended = 3
   };

   enum wallet_record_type_enum
   {
      master_key_record_type     = 0,
      account_record_type        = 1,
      key_record_type            = 2,
      transaction_record_type    = 3,
      balance_record_type        = 6,
      property_record_type       = 7,
      market_order_record_type   = 8, /* No longer used for now */
      setting_record_type        = 9
   };

   struct escrow_summary
   {
      /** the transaction ID that created the escrow balance */
      transaction_id_type creating_transaction_id;
      balance_id_type     balance_id;
      /** the amount of money still held in escrow */
      asset               balance;
      /** the account name of the escrow agent */
      string              sender_account_name;
      string              receiver_account_name;
      string              escrow_agent_account_name;
      digest_type         agreement_digest;
   };

   struct generic_wallet_record
   {
       generic_wallet_record():type(0){}

       template<typename RecordType>
       generic_wallet_record( const RecordType& rec )
       :type( int(RecordType::type) ),data(rec)
       { }

       template<typename RecordType>
       RecordType as()const;

       int32_t get_wallet_record_index()const
       { try {
          FC_ASSERT( data.is_object() );
          FC_ASSERT( data.get_object().contains( "index" ) );
          return data.get_object()["index"].as<int32_t>();
       } FC_RETHROW_EXCEPTIONS( warn, "" ) }

       fc::enum_type<uint8_t,wallet_record_type_enum>   type;
       fc::variant                                      data;
   };

   template<wallet_record_type_enum RecordType>
   struct base_record
   {
      enum { type  = RecordType };

      base_record( int32_t idx = 0 ):wallet_record_index(idx){}
      int32_t wallet_record_index;
   };

   enum property_enum
   {
      version,
      next_record_number,
      next_child_key_index,
      automatic_backups,
      transaction_scanning,
      last_unlocked_scanned_block_number,
      default_transaction_priority_fee,
      transaction_expiration_sec
   };

   /** Used to store key/value property pairs.
    */
   struct wallet_property
   {
       wallet_property( property_enum k = next_record_number,
                        fc::variant v = fc::variant() )
       :key(k),value(v){}

       fc::enum_type<int32_t, property_enum> key;
       fc::variant                           value;
   };

   /**
    *  Contacts are tracked by the hash of their receive key
    */
   struct account_data : public bts::blockchain::account_record
   {
       bool                             is_my_account = false;
       int8_t                           approved = 0;
       bool                             is_favorite = false;
       bool                             block_production_enabled = false;
       uint32_t                         last_used_gen_sequence = 0;
       variant                          private_data;
   };

   template<typename RecordTypeName, wallet_record_type_enum RecordTypeNumber>
   struct wallet_record : public base_record<RecordTypeNumber>, public RecordTypeName
   {
      wallet_record(){}
      wallet_record( const RecordTypeName& rec, int32_t wallet_record_index = 0 )
      :base_record<RecordTypeNumber>(wallet_record_index),RecordTypeName(rec){}
   };

   struct master_key
   {
       std::vector<char>              encrypted_key;
       fc::sha512                     checksum;

       bool                           validate_password( const fc::sha512& password )const;
       extended_private_key           decrypt_key( const fc::sha512& password )const;
       void                           encrypt_key( const fc::sha512& password,
                                                   const extended_private_key& k );
   };

   struct key_data
   {
       address                          account_address;
       public_key_type                  public_key;
       std::vector<char>                encrypted_private_key;
       bool                             valid_from_signature = false;
       optional<string>                 memo; // this memo is not used for anything.
       uint32_t                         gen_seq_number = 0;

       address                          get_address()const { return address( public_key ); }
       bool                             has_private_key()const;
       void                             encrypt_private_key( const fc::sha512& password,
                                                             const private_key_type& );
       private_key_type                 decrypt_private_key( const fc::sha512& password )const;
   };

   struct ledger_entry
   {
       optional<public_key_type> from_account;
       optional<public_key_type> to_account;
       asset                     amount;
       string                    memo;
       optional<public_key_type> memo_from_account;
   };

   struct transaction_data
   {
       /*
        * record_id
        * - non-virtual transactions: trx.id()
        * - virtual genesis claims: fc::ripemd160::hash( owner_account_name )
        * - virtual market bids: fc::ripemd160::hash( block_num + string( mtrx.bid_owner ) + string( mtrx.ask_owner ) )
        * - virtual market asks: fc::ripemd160::hash( block_num + string( mtrx.ask_owner ) + string( mtrx.bid_owner ) )
        */
       transaction_id_type       record_id;
       uint32_t                  block_num = 0;
       bool                      is_virtual = false;
       bool                      is_confirmed = false;
       bool                      is_market = false;
       signed_transaction        trx;
       vector<ledger_entry>      ledger_entries;
       asset                     fee;
       fc::time_point_sec        created_time;
       fc::time_point_sec        received_time;
       vector<address>           extra_addresses;
   };

   // don't use -- work in progress
   struct transaction_ledger_entry
   {
       transaction_id_type                          id;
       uint32_t                                     block_num = -1;
       time_point_sec                               timestamp = time_point_sec( -1 );

       // e.g. { name, INCOME-name, ISSUER-name, `snapshot address`, {ASK,BID,SHORT,MARGIN}-id, FEE }
       map<string, map<asset_id_type, share_type>>  delta_amounts;

       optional<transaction_id_type>                transaction_id;

       // only really useful for titan transfers
       map<uint16_t, string>                        delta_labels;

       map<uint16_t, string>                        operation_notes;

       bool is_confirmed()const { return block_num != -1; }
       bool is_virtual()const   { return !transaction_id.valid(); }

       friend bool operator < ( const transaction_ledger_entry& a, const transaction_ledger_entry& b )
       {
           if( a.is_confirmed() == b.is_confirmed() )
               return std::tie( a.block_num, a.timestamp, a.id ) < std::tie( b.block_num, b.timestamp, b.id );
           else
               return std::tie( a.timestamp, a.id ) < std::tie( b.timestamp, b.id );
       }
   };

   struct pretty_transaction_experimental : transaction_ledger_entry
   {
       vector<std::pair<string, asset>> inputs;
       vector<std::pair<string, asset>> outputs;
       mutable vector<std::pair<string, asset>> balances;
       vector<string>                   notes;
   };

#if 0
   struct market_order_status
   {
      order_type_enum get_type()const;
      order_id_type   get_id()const;

      asset           get_balance()const; // funds available for this order
      price           get_price()const;
      asset           get_quantity()const;
      asset           get_proceeds()const;

      bts::blockchain::market_order        order;
      share_type                           proceeds = 0;
      unordered_set<transaction_id_type>   transactions;
   };
#endif

   /* Used to store GUI preferences and such */
   struct setting
   {
      setting(){};
      setting(string name, variant value):name(name),value(value){};
      string       name;
      variant      value;
   };

   /** cached blockchain data */
   // TODO: Only cache balance ids
   typedef wallet_record< bts::blockchain::balance_record, balance_record_type     >  wallet_balance_record;

   /** records unique to the wallet */
   typedef wallet_record< transaction_data,                transaction_record_type >  wallet_transaction_record;
   typedef wallet_record< master_key,                      master_key_record_type  >  wallet_master_key_record;
   typedef wallet_record< key_data,                        key_record_type         >  wallet_key_record;
   // TODO: Do not derive from blockchain account record
   typedef wallet_record< account_data,                    account_record_type     >  wallet_account_record;
   typedef wallet_record< wallet_property,                 property_record_type    >  wallet_property_record;
   //typedef wallet_record< market_order_status,             market_order_record_type>  wallet_market_order_status_record;
   typedef wallet_record< setting,                         setting_record_type     >  wallet_setting_record;

   typedef optional< wallet_transaction_record >            owallet_transaction_record;
   typedef optional< wallet_master_key_record >             owallet_master_key_record;
   typedef optional< wallet_key_record >                    owallet_key_record;
   typedef optional< wallet_account_record >                owallet_account_record;
   typedef optional< wallet_property_record >               owallet_property_record;
   typedef optional< wallet_balance_record >                owallet_balance_record;
   //typedef optional< wallet_market_order_status_record >    owallet_market_order_record;
   typedef optional< wallet_setting_record >                owallet_setting_record;

} } // bts::wallet

FC_REFLECT( bts::wallet::escrow_summary,
            (creating_transaction_id)
            (balance_id)
            (balance)
            (sender_account_name)
            (receiver_account_name)
            (escrow_agent_account_name)
            (agreement_digest)
          )

FC_REFLECT_ENUM( bts::wallet::wallet_record_type_enum,
        (master_key_record_type)
        (account_record_type)
        (key_record_type)
        (transaction_record_type)
        (balance_record_type)
        (property_record_type)
        (market_order_record_type)
        (setting_record_type)
        )

FC_REFLECT( bts::wallet::generic_wallet_record,
        (type)
        (data)
        )

FC_REFLECT_ENUM( bts::wallet::property_enum,
        (version)
        (next_record_number)
        (next_child_key_index)
        (automatic_backups)
        (transaction_scanning)
        (last_unlocked_scanned_block_number)
        (default_transaction_priority_fee)
        (transaction_expiration_sec)
        )

FC_REFLECT( bts::wallet::wallet_property,
        (key)
        (value)
        )

FC_REFLECT_DERIVED( bts::wallet::account_data, (bts::blockchain::account_record),
        (is_my_account)
        (approved)
        (is_favorite)
        (block_production_enabled)
        (last_used_gen_sequence)
        (private_data)
        )

FC_REFLECT( bts::wallet::master_key,
        (encrypted_key)
        (checksum)
        )

FC_REFLECT( bts::wallet::key_data,
        (account_address)
        (public_key)
        (encrypted_private_key)
        (valid_from_signature)
        (memo)
        (gen_seq_number)
        )

FC_REFLECT( bts::wallet::ledger_entry,
        (from_account)
        (to_account)
        (amount)
        (memo)
        (memo_from_account)
        )

FC_REFLECT( bts::wallet::transaction_data,
        (record_id)
        (block_num)
        (is_virtual)
        (is_confirmed)
        (is_market)
        (trx)
        (ledger_entries)
        (fee)
        (created_time)
        (received_time)
        (extra_addresses)
        )

//FC_REFLECT( bts::wallet::market_order_status, (order)(proceeds)(transactions) )

FC_REFLECT( bts::wallet::setting,
        (name)
        (value)
        )

// do not use -- see notes above
FC_REFLECT( bts::wallet::transaction_ledger_entry,
        (id)
        (block_num)
        (timestamp)
        (delta_amounts)
        (transaction_id)
        (delta_labels)
        (operation_notes)
        )

FC_REFLECT_DERIVED( bts::wallet::pretty_transaction_experimental, (bts::wallet::transaction_ledger_entry),
        (inputs)
        (outputs)
        (balances)
        (notes)
        )

/**
 *  Implement generic reflection for wallet record types
 */
namespace fc {

   template<typename T, bts::wallet::wallet_record_type_enum N>
   struct get_typename< bts::wallet::wallet_record<T,N> >
   {
      static const char* name()
      {
         static std::string _name =  get_typename<T>::name() + std::string("_record");
         return _name.c_str();
      }
   };

   template<typename Type, bts::wallet::wallet_record_type_enum N>
   struct reflector<bts::wallet::wallet_record<Type,N>>
   {
      typedef bts::wallet::wallet_record<Type,N>  type;
      typedef fc::true_type is_defined;
      typedef fc::false_type is_enum;
      enum member_count_enum {
         local_member_count = 1,
         total_member_count = local_member_count + reflector<Type>::total_member_count
      };

      template<typename Visitor>
      static void visit( const Visitor& visitor )
      {
          {
            typedef decltype(((bts::wallet::base_record<N>*)nullptr)->wallet_record_index) member_type;
            visitor.TEMPLATE operator()<member_type,bts::wallet::base_record<N>,&bts::wallet::base_record<N>::wallet_record_index>( "index" );
          }

          fc::reflector<Type>::visit( visitor );
      }
   };
} // namespace fc

namespace bts { namespace wallet {
       template<typename RecordType>
       RecordType generic_wallet_record::as()const
       {
          FC_ASSERT( (wallet_record_type_enum)type == RecordType::type, "",
                     ("type",type)
                     ("WithdrawType",(wallet_record_type_enum)RecordType::type) );

          return data.as<RecordType>();
       }
} }
