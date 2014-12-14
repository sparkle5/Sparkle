#include <boost/test/unit_test.hpp>
#include <bts/api/global_api_logger.hpp>
#include <bts/blockchain/chain_database.hpp>
#include <bts/blockchain/genesis_config.hpp>
#include <bts/wallet/wallet.hpp>
#include <bts/client/api_logger.hpp>
#include <bts/client/client.hpp>
#include <bts/client/messages.hpp>
#include <bts/cli/cli.hpp>
#include <bts/blockchain/config.hpp>
#include <bts/blockchain/time.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/json.hpp>
#include <fc/thread/thread.hpp>
#include <bts/utilities/key_conversion.hpp>

#include <fc/network/http/connection.hpp>

#include <fc/string.hpp>

#include <boost/filesystem.hpp>
#include <boost/bind.hpp>

#include <iostream>
#include <fstream>


using namespace bts::blockchain;
using namespace bts::wallet;
using namespace bts::utilities;
using namespace bts::client;
using namespace bts::cli;

BOOST_AUTO_TEST_CASE( public_key_type_test )
{
   try {
    auto k1 = fc::ecc::private_key::generate().get_public_key();
    auto k2 = fc::ecc::private_key::generate().get_public_key();
    auto k3 = fc::ecc::private_key::generate().get_public_key();

    FC_ASSERT( public_key_type( std::string( public_key_type(k1) ) ) == k1);
    FC_ASSERT( public_key_type( std::string( public_key_type(k2) ) ) == k2);
    FC_ASSERT( public_key_type( std::string( public_key_type(k3) ) ) == k3);
  } catch ( const fc::exception& e )
   {
      elog( "${e}", ("e",e.to_detail_string()) );
      throw;
   }
}

BOOST_AUTO_TEST_CASE( wif_format_test )
{
  try {
   auto priv_key = fc::variant( "0c28fca386c7a227600b2fe50b7cae11ec86d3bf1fbe471be89827e19d72aa1d" ).as<fc::ecc::private_key>();
   FC_ASSERT( bts::utilities::key_to_wif(priv_key) == "5HueCGU8rMjxEXxiPuD5BDku4MkFqeZyd4dZ1jvhTVqvbTLvyTJ" );
   FC_ASSERT( bts::utilities::wif_to_key( "5HueCGU8rMjxEXxiPuD5BDku4MkFqeZyd4dZ1jvhTVqvbTLvyTJ" ).valid() );
   wif_to_key( key_to_wif( fc::ecc::private_key::generate() ) );
  } FC_LOG_AND_RETHROW()
}

template<typename T>
void produce_block( T my_client )
{
      auto head_num = my_client->get_chain()->get_head_block_num();
      const auto& delegates = my_client->get_wallet()->get_my_delegates( enabled_delegate_status | active_delegate_status );
      auto next_block_time = my_client->get_wallet()->get_next_producible_block_timestamp( delegates );
      FC_ASSERT( next_block_time.valid() );
      bts::blockchain::advance_time( (int32_t)((*next_block_time - bts::blockchain::now()).count()/1000000) );
      auto b = my_client->get_chain()->generate_block(*next_block_time);
      my_client->get_wallet()->sign_block( b );
      my_client->get_node()->broadcast( bts::client::block_message( b ) );
      FC_ASSERT( head_num+1 == my_client->get_chain()->get_head_block_num() );
      bts::blockchain::advance_time( 1 );
}


/***
 *  This test case is designed to grow for ever and never shrink.  It is a complete scripted history
 *  of operations that should always work based upon a generated genesis condition.
 */
BOOST_AUTO_TEST_CASE( master_test )
{ try {
   bts::blockchain::start_simulated_time(fc::time_point::from_iso_string( "20200101T000000" ));

   vector<fc::ecc::private_key> delegate_private_keys;

   genesis_block_config config;
   config.timestamp         = bts::blockchain::now();

   for( uint32_t i = 0; i < BTS_BLOCKCHAIN_NUM_DELEGATES; ++i )
   {
      name_config delegate_account;
      delegate_account.name = "delegate" + fc::to_string(i);
      delegate_private_keys.push_back( fc::ecc::private_key::generate() );
      auto delegate_public_key = delegate_private_keys.back().get_public_key();
      delegate_account.owner = delegate_public_key;
      delegate_account.delegate_pay_rate = 100;

      config.names.push_back(delegate_account);
      config.balances.push_back( std::make_pair( pts_address(fc::ecc::public_key_data(delegate_account.owner)), BTS_BLOCKCHAIN_INITIAL_SHARES/BTS_BLOCKCHAIN_NUM_DELEGATES) );
   }

   fc::temp_directory clienta_dir;
   fc::json::save_to_file( config, clienta_dir.path() / "genesis.json" );

   fc::temp_directory clientb_dir;
   fc::json::save_to_file( config, clientb_dir.path() / "genesis.json" );

   bts::net::simulated_network_ptr sim_network = std::make_shared<bts::net::simulated_network>("wallet_tests");

   bts::client::client_ptr clienta = std::make_shared<bts::client::client>("wallet_tests", sim_network);
   clienta->open( clienta_dir.path(), clienta_dir.path() / "genesis.json" );
   clienta->configure_from_command_line( 0, nullptr );
   clienta->start().wait();

   bts::client::client_ptr clientb = std::make_shared<bts::client::client>("wallet_tests", sim_network);
   clientb->open( clientb_dir.path(), clientb_dir.path() / "genesis.json" );
   clientb->configure_from_command_line( 0, nullptr );
   clientb->start().wait();

   std::cerr << clientb->execute_command_line( "help" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_create walletb masterpassword" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_create walleta masterpassword" ) << "\n";

   int even = 0;
   for( auto key : delegate_private_keys )
   {
      if( even >= 30 )
      {
         if( (even++)%2 )
         {
            std::cerr << "client a key: "<< even<<" "<< clienta->execute_command_line( "wallet_import_private_key " + key_to_wif( key  ) ) << "\n";
         }
         else
         {
            std::cerr << "client b key: "<< even<<" "<< clientb->execute_command_line( "wallet_import_private_key " + key_to_wif( key  ) ) << "\n";
         }
         if( even >= 34 ) break;
      }
      else ++even;
   }

   wlog( "------------------  CLIENT A  -----------------------------------" );
   std::cerr << clienta->execute_command_line( "wallet_list_receive_accounts" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_balance" ) << "\n";
   std::cerr << clienta->execute_command_line( "unlock 999999999 masterpassword" ) << "\n";
   std::cerr << clienta->execute_command_line( "scan 0 100" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_balance" ) << "\n";
   std::cerr << clienta->execute_command_line( "close" ) << "\n";
   std::cerr << clienta->execute_command_line( "open walleta" ) << "\n";
   std::cerr << clienta->execute_command_line( "unlock 99999999 masterpassword" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_balance" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_balance delegate31" ) << "\n";

   wlog( "------------------  CLIENT B  -----------------------------------" );
   std::cerr << clientb->execute_command_line( "wallet_list_receive_accounts" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_balance" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_balance delegate30" ) << "\n";
   std::cerr << clientb->execute_command_line( "unlock 999999999 masterpassword" ) << "\n";

   std::cerr << clientb->execute_command_line( "wallet_account_create b-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_balance b-account" ) << "\n";

   std::cerr << clientb->execute_command_line( "wallet_account_register b-account delegate30" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   produce_block( clienta );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   std::cerr << clientb->execute_command_line( "wallet_list_receive_accounts" );
   std::cerr << clientb->execute_command_line( "wallet_account_update_registration b-account delegate30 { \"ip\":\"localhost\"} true" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   produce_block( clienta );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   std::cerr << clientb->execute_command_line( "wallet_list_receive_accounts" );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   std::cerr << clienta->execute_command_line( "wallet_transfer 33 XTS delegate31 b-account first-memo" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_transaction_history delegate31" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_transaction_history b-account" ) << "\n";
   wlog( "------------------  CLIENT B  -----------------------------------" );
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history b-account" ) << "\n";
   produce_block( clientb );
   wlog( "------------------  CLIENT A  -----------------------------------" );
   std::cerr << clienta->execute_command_line( "wallet_account_transaction_history delegate31" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_transaction_history b-account" ) << "\n";
   wlog( "------------------  CLIENT B  -----------------------------------" );
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history b-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_create c-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_transfer 10 XTS b-account c-account to-me" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history b-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history c-account" ) << "\n";
   produce_block( clientb );
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history c-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "blockchain_list_delegates" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_set_delegate_trust_level b-account 1" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_list_receive_accounts" ) << "\n";
   std::cerr << clientb->execute_command_line( "balance" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_transfer 100000 XTS delegate32 c-account to-me" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_transfer 100000 XTS delegate30 c-account to-me" ) << "\n";
   wlog( "------------------  CLIENT A  -----------------------------------" );
   std::cerr << clienta->execute_command_line( "wallet_set_delegate_trust_level b-account 1" ) << "\n";
   // TODO: this should throw an exception from the wallet regarding delegate_vote_limit, but it produces
   // the transaction anyway.
   // TODO: before fixing the wallet production side to include multiple outputs and spread the vote,
   // the transaction history needs to show the transaction as an 'error' rather than 'pending' and
   // properly display the reason for the user.
   // TODO: provide a way to cancel transactions that are pending.
   std::cerr << clienta->execute_command_line( "wallet_transfer 100000 XTS delegate31 b-account to-b" ) << "\n";
   wlog( "------------------  CLIENT B  -----------------------------------" );
   produce_block( clientb );
   std::cerr << clientb->execute_command_line( "balance" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history c-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "blockchain_list_delegates" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_asset_create USD Dollar b-account \"paper bucks\" null 1000000000 1000" ) << "\n";
   produce_block( clientb );
   std::cerr << clientb->execute_command_line( "blockchain_list_assets" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_asset_issue 1000 USD c-account \"iou\"" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history b-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history c-account" ) << "\n";
   produce_block( clientb );
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history b-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history c-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_transfer 20 USD c-account delegate31 c-d31" ) << "\n";
   wlog( "------------------  CLIENT A  -----------------------------------" );
   produce_block( clienta );
   wlog( "------------------  CLIENT B  -----------------------------------" );
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history c-account" ) << "\n";
   wlog( "------------------  CLIENT A  -----------------------------------" );
   std::cerr << clienta->execute_command_line( "wallet_account_transaction_history delegate31" ) << "\n";
   wlog( "------------------  CLIENT B  -----------------------------------" );
   std::cerr << clientb->execute_command_line( "balance" ) << "\n";
   std::cerr << clientb->execute_command_line( "bid c-account 120 XTS 4.50 USD" ) << "\n";
   std::cerr << clientb->execute_command_line( "bid c-account 40 XTS 2.50 USD" ) << "\n";
   produce_block( clientb );
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history c-account" ) << "\n";
   std::cerr << clientb->execute_command_line( "balance" ) << "\n";
   std::cerr << clientb->execute_command_line( "blockchain_market_list_bids USD XTS" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_market_order_list USD XTS" ) << "\n";
   auto result = clientb->wallet_market_order_list( "USD", "XTS" );
   std::cerr << clientb->execute_command_line( "wallet_market_cancel_order " + string( result.begin()->first ) ) << "\n";
   produce_block( clientb );
   std::cerr << clientb->execute_command_line( "wallet_market_order_list USD XTS" ) << "\n";
   std::cerr << clientb->execute_command_line( "blockchain_market_list_bids USD XTS" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history" ) << "\n";
   std::cerr << clientb->execute_command_line( "balance" ) << "\n";
   result = clientb->wallet_market_order_list( "USD", "XTS" );
   std::cerr << clientb->execute_command_line( "wallet_market_cancel_order " + string( result.begin()->first ) ) << "\n";
   produce_block( clientb );
   std::cerr << clientb->execute_command_line( "wallet_market_order_list USD XTS" ) << "\n";
   std::cerr << clientb->execute_command_line( "blockchain_market_list_bids USD XTS" ) << "\n";
   std::cerr << clientb->execute_command_line( "wallet_account_transaction_history" ) << "\n";
   std::cerr << clientb->execute_command_line( "balance" ) << "\n";
   // THis is an invalid order
   //std::cerr << clientb->execute_command_line( "bid c-account 210 USD 5.40 XTS" ) << "\n";
   //produce_block( clientb );
  // std::cerr << clientb->execute_command_line( "wallet_account_transaction_history c-account" ) << "\n";
 //  std::cerr << clientb->execute_command_line( "balance" ) << "\n";

   //std::cerr << clientb->execute_command_line( "wallet_list_receive_accounts" ) << "\n";
   //std::cerr << clientb->execute_command_line( "wallet_account_balance" ) << "\n";


   // Test "." in names TODO #289
   std::cerr << clienta->execute_command_line( "wallet_account_create test.a" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_create a" ) << "\n";
   std::cerr << clienta->execute_command_line( "wallet_account_register a delegate31" ) << "\n";
   produce_block(clienta);
   std::cerr << clienta->execute_command_line( "wallet_account_register test.a delegate31" ) << "\n";
   produce_block(clienta);

   // you should be able to rename back to a local account you had already
   std::cerr << clienta->execute_command_line( "wallet_account_create firstname" );
   std::cerr << clienta->execute_command_line( "wallet_account_rename firstname secondname" );
   std::cerr << clienta->execute_command_line( "wallet_account_rename secondname firstname" );

} FC_LOG_AND_RETHROW() }



class test_file
{
  fc::path _result_file;
  fc::path _expected_result_file;
public:
       test_file(fc::future<void> done, fc::path result_file, fc::path expected_result_file)
         : _result_file(result_file), _expected_result_file(expected_result_file), client_done(done) {}

  bool compare_files(); //compare two files, return true if the files match
  bool compare_files_2();
  void prepare_string(std::string &str);

  fc::future<void> client_done;
};

bool test_file::compare_files()
{
  //first check if files are equal size, if not, files are different
  uintmax_t result_file_size = boost::filesystem::file_size(_result_file.string());
  uintmax_t expected_result_file_size = boost::filesystem::file_size(_expected_result_file.string());
  bool file_sizes_equal = (result_file_size == expected_result_file_size);
  if (!file_sizes_equal)
    return false;

  //files may be equal since they have the same size, so check further
  std::ifstream lhs(_result_file.string().c_str());
  std::ifstream rhs(_expected_result_file.string().c_str());

  typedef std::istreambuf_iterator<char> istreambuf_iterator;
  return std::equal( istreambuf_iterator(lhs),  istreambuf_iterator(), istreambuf_iterator(rhs));
}

bool test_file::compare_files_2()
{
  std::ifstream lhs(_result_file.string().c_str());
  std::ifstream rhs(_expected_result_file.string().c_str());

  std::string l_line;
  std::string r_line;

  while(!lhs.eof() || !rhs.eof())
  {
    std::getline(lhs, l_line);
    std::getline(rhs, r_line);

    prepare_string(l_line);
    prepare_string(r_line);

    if(l_line.compare(r_line) != 0)
      return false;
  }

  return true;
}

void test_file::prepare_string(std::string &str)
{
  for(;;)
  {
    std::size_t found_start = str.find("<d-ign>");
    std::size_t found_stop = str.find("</d-ign>");

    if(found_start == std::string::npos && found_stop == std::string::npos)
      return;

    if(found_start == std::string::npos)
      str.erase(0, found_stop);
    else if(found_stop == std::string::npos)
      str.erase(str.begin() + found_start, str.end());
    else if(found_start < found_stop)
      str.erase(found_start, found_stop - found_start + string("</d-ign>").size());
    else
    {
      str.erase(str.begin() + found_start, str.end());
      str.erase(0, found_stop);
    }
  }
}

#ifdef WIN32
#include <Windows.h>
char** CommandLineToArgvA(const char* CmdLine, int* _argc)
{
  char** argv;
  char*  _argv;
  unsigned long   len;
  uint32_t   argc;
  CHAR   a;
  uint32_t   i, j;

  bool  in_QM;
  bool  in_TEXT;
  bool  in_SPACE;

  len = strlen(CmdLine);
  i = ((len+2)/2)*sizeof(void*) + sizeof(void*);

  argv = (char**)GlobalAlloc(GMEM_FIXED,
      i + (len+2)*sizeof(CHAR));

  _argv = (char*)(((PUCHAR)argv)+i);

  argc = 0;
  argv[argc] = _argv;
  in_QM = false;
  in_TEXT = false;
  in_SPACE = true;
  i = 0;
  j = 0;

  while( a = CmdLine[i] ) {
      if(in_QM) {
          if(a == '\"') {
              in_QM = false;
          } else {
              _argv[j] = a;
              j++;
          }
      } else {
          switch(a) {
          case '\"':
              in_QM = true;
              in_TEXT = true;
              if(in_SPACE) {
                  argv[argc] = _argv+j;
                  argc++;
              }
              in_SPACE = false;
              break;
          case ' ':
          case '\t':
          case '\n':
          case '\r':
              if(in_TEXT) {
                  _argv[j] = '\0';
                  j++;
              }
              in_TEXT = false;
              in_SPACE = true;
              break;
          default:
              in_TEXT = true;
              if(in_SPACE) {
                  argv[argc] = _argv+j;
                  argc++;
              }
              _argv[j] = a;
              j++;
              in_SPACE = false;
              break;
          }
      }
      i++;
  }
  _argv[j] = '\0';
  argv[argc] = NULL;

  (*_argc) = argc;
  return argv;
}
#else //UNIX
  #include <wordexp.h>
#endif

using namespace boost;
#include <bts/utilities/deterministic_openssl_rand.hpp>
#include <bts/utilities/key_conversion.hpp>

void create_genesis_block(fc::path genesis_json_file)
{
   vector<fc::ecc::private_key> delegate_private_keys;

   genesis_block_config config;
   config.timestamp         = bts::blockchain::now();

   // set our fake random number generator to generate deterministic keys
   bts::utilities::set_random_seed_for_testing( fc::sha512::hash( string( "genesis" ) ) );
   std::ofstream key_stream( genesis_json_file.string() + ".keypairs" );
   //create a script for importing the delegate keys
   std::ofstream delegate_key_import_stream(genesis_json_file.string() + ".log");
   delegate_key_import_stream << CLI_PROMPT_SUFFIX " debug_enable_output false" << std::endl;
   std::cout << "*** creating delegate public/private key pairs ***" << std::endl;
   for( uint32_t i = 0; i < BTS_BLOCKCHAIN_NUM_DELEGATES; ++i )
   {
      name_config delegate_account;
      delegate_account.name = "delegate" + fc::to_string(i);
      fc::ecc::private_key delegate_private_key = fc::ecc::private_key::generate();
      delegate_private_keys.push_back( delegate_private_key );

      auto delegate_public_key =delegate_private_key.get_public_key();
      delegate_account.owner = delegate_public_key;
      delegate_account.delegate_pay_rate = 100;

      config.names.push_back(delegate_account);
      config.balances.push_back( std::make_pair( pts_address(fc::ecc::public_key_data(delegate_account.owner)), BTS_BLOCKCHAIN_INITIAL_SHARES/BTS_BLOCKCHAIN_NUM_DELEGATES) );

      //output public/private key pair for each delegate to a file
      string wif_key = bts::utilities::key_to_wif( delegate_private_key );
      key_stream << std::string(delegate_account.owner) << "   " << wif_key << std::endl;
      //add command to import the delegate keys into a client
      delegate_key_import_stream << CLI_PROMPT_SUFFIX " wallet_import_private_key " << wif_key << " " << delegate_account.name << " false" << std::endl;
   }
   delegate_key_import_stream << CLI_PROMPT_SUFFIX " debug_enable_output true" << std::endl;

   fc::json::save_to_file( config, genesis_json_file);
}

BOOST_AUTO_TEST_CASE(make_genesis_block)
{
  fc::path genesis_json_file =  "test_genesis.json";
  create_genesis_block(genesis_json_file);
}

void run_regression_test(fc::path test_dir, bool with_network)
{
  bts::blockchain::start_simulated_time(fc::time_point_sec::min());
  // set our fake random number generator to generate deterministic keys
  bts::utilities::set_random_seed_for_testing( fc::sha512::hash( string( "regression" ) ) );
  //  open testconfig file
  //  for each line in testconfig file
  //    add a verify_file object that knows the name of the input command file and the generated log file
  //    start a process with that command line
  //  wait for all processes to shutdown
  //  for each verify_file object,
  //    compare generated log files in datadirs to golden reference file (i.e. input command files)

  // caller of this routine should have made sure we are already in bitshares/test dir.
  // so we pop dirs to create regression_tests_results as sibling to bitshares source directory
  // (because we don't want the test results to be inadvertantly added to git repo).
  fc::path original_working_directory = boost::filesystem::current_path();
  fc::path regression_test_output_directory = original_working_directory.parent_path().parent_path();
  regression_test_output_directory /= "regression_tests_output";

  // Create an expected output file in the test subdir for the test output.
  fc::path test_output_dir = regression_test_output_directory / test_dir;
  boost::filesystem::create_directories(test_output_dir);
  try
  {
    std::cout << "*** Executing " << test_dir.string() << std::endl;

    boost::filesystem::current_path(test_dir.string());

    //create a small genesis block to reduce test startup time
    //fc::temp_directory temp_dir;
    //fc::path genesis_json_file =  temp_dir.path() / "genesis.json";
    //create_genesis_block(genesis_json_file);
    //DLN: now we just used the genesis file already committed to repo, update it
    //     using "-t make_genesis_block" if desired.
    fc::path genesis_json_file = "../../test_genesis.json";


    //open test configuration file (contains one line per client to create)
    fc::path test_config_file_name = "test.config";
    std::ifstream test_config_file(test_config_file_name.string());
#if BTS_GLOBAL_API_LOG
    fc::path test_glog_file_name = test_output_dir / "glog.out";
    fc::ostream_ptr test_glog_file = std::make_shared<fc::ofstream>(test_glog_file_name);
    bts::client::stream_api_logger glogger(test_glog_file);
    glogger.connect();
#endif

    //create one client per line and run each client's input commands
    auto sim_network = std::make_shared<bts::net::simulated_network>("wallet_tests");
    vector<test_file> tests;
    string line;
    std::vector<bts::client::client_ptr> clients;
    while (std::getline(test_config_file, line))
    {
      line = fc::trim( line );
      if( line.length() == 0 )
          continue;
      if( (line.length() >= 8) && (line.substr(0, 8) == "genesis ") )
      {
          genesis_json_file = line.substr( 8 );
          continue;
      }
      line += " --disable-default-peers ";
      line += " --log-commands ";
      line += " --ulog=0 ";
      line += " --min-delegate-connection-count=0 ";
      line += " --upnp=false ";

      //append genesis_file to load to command-line for now (later should be pre-created in test dir I think)
      line += " --genesis-config " + genesis_json_file.string();

      //if no data-dir specified, put in ../bitshares/regression_tests/${test dir}/${client_name}
      string client_name = line.substr(0, line.find(' '));
      size_t data_dir_position = line.find("--data-dir");
      if (data_dir_position == string::npos)
      {
        fc::path default_data_dir = regression_test_output_directory / test_dir / client_name;
        line += " --data-dir=" + default_data_dir.string();
        fc::remove_all(default_data_dir);
      }

      std::cout << "cmd-line args=" << line << std::endl;
      //parse line into argc/argv format for boost program_options
      int argc = 0;
      char** argv = nullptr;
    #ifndef WIN32 // then UNIX
      //use wordexp to get argv/arc
      wordexp_t wordexp_result;
      wordexp(line.c_str(), &wordexp_result, 0);
      auto option_variables = parse_option_variables(wordexp_result.we_wordc, wordexp_result.we_wordv);
      argv = wordexp_result.we_wordv;
      argc = wordexp_result.we_wordc;
    #else
      //use ExpandEnvironmentStrings and CommandLineToArgv to get argv/arc
      char expanded_line[40000];
      ExpandEnvironmentStrings(line.c_str(),expanded_line,sizeof(expanded_line));
      argv = CommandLineToArgvA(expanded_line,&argc);
      auto option_variables = bts::client::parse_option_variables(argc, argv);
    #endif

      //extract input command files from cmdline options and concatenate into
      //one expected output file so that we can compare against output log
      std::ostringstream expected_output_filename;
      expected_output_filename << "expected_output_" << client_name << ".log";
      fc::path expected_output_file = test_output_dir / expected_output_filename.str();
      std::ofstream expected_output_stream(expected_output_file.string());

      std::vector<string> input_logs = option_variables["input-log"].as<std::vector<string>>();
      for (string input_log : input_logs)
      {
        std::cout << "input-log=" << input_log << std::endl;
        std::ifstream input_stream(input_log);
        expected_output_stream << input_stream.rdbuf();
      }
      expected_output_stream.close();

      fc::future<void> client_done;

      //run client with cmdline options
      if (with_network)
      {
        FC_ASSERT(false, "Not implemented yet!");
      }
      else
      {
        bts::client::client_ptr client = std::make_shared<bts::client::client>("wallet_tests", sim_network);
        clients.push_back(client);
        client->configure_from_command_line(argc, argv);
      #if BTS_GLOBAL_API_LOG
        client->set_client_debug_name(client_name);
      #endif
        client_done = client->start();
      }


    #ifndef WIN32 // then UNIX
      wordfree(&wordexp_result);
    #else
      GlobalFree(argv);
    #endif

      //add a test that compares input command file to client's log file
      fc::path result_file = ::get_data_dir(option_variables) / "console.log";
      tests.push_back( test_file(client_done, result_file, expected_output_file) );
    } //end while not end of test config file

    //check each client's log file against it's golden reference log file
    for (test_file current_test : tests)
    {
      //current_test.compare_files();
      current_test.client_done.wait();
      BOOST_CHECK_MESSAGE(current_test.compare_files_2(), "Results mismatch with golden reference log");
    }

#if BTS_GLOBAL_API_LOG
    glogger.close();
#endif
  }
  catch ( const fc::exception& e )
  {
    BOOST_FAIL("Caught unexpected exception:" << e.to_detail_string() );
  }

  //restore original working directory
  boost::filesystem::current_path(original_working_directory);
}

//#define ENABLE_REPLAY_CHAIN_DATABASE_TESTS
#ifdef ENABLE_REPLAY_CHAIN_DATABASE_TESTS
// A simple test that feeds a chain database from a normal client installation block-by-block to
// the client directly, bypassing all networking code.
void replay_chain_database()
{
  const uint32_t granularity = BTS_BLOCKCHAIN_BLOCKS_PER_DAY;

  fc::temp_directory client_dir;
  fc::remove_all(client_dir.path());
  //auto sim_network = std::make_shared<bts::net::simulated_network>();
  bts::net::simulated_network_ptr sim_network = std::make_shared<bts::net::simulated_network>("wallet_tests");
  bts::client::client_ptr client = std::make_shared<bts::client::client>("wallet_tests", sim_network);
  //client->open( client_dir.path() );
  std::string client_dir_string = client_dir.path().string();
  char *fake_argv[] = {"bitshares_client", "--data-dir", (char*)client_dir_string.c_str(), "--accept-incoming-connections=false", "--disable-default-peers", "--upnp=false"};
  client->configure_from_command_line(sizeof(fake_argv) / sizeof(fake_argv[0]), fake_argv);
  //client->configure_from_command_line(0, nullptr);
  fc::future<void> client_done = client->start();
  fc::usleep(fc::milliseconds(10));
  bts::blockchain::chain_database_ptr source_blockchain = std::make_shared<bts::blockchain::chain_database>();
  fc::path test_net_chain_dir("C:\\Users\\Administrator\\AppData\\Roaming\\BitShares X");
  source_blockchain->open(test_net_chain_dir / "chain", fc::optional<fc::path>());
  BOOST_TEST_MESSAGE("Opened source blockchain containing " << source_blockchain->get_head_block_num() << " blocks");
  unsigned total_blocks_to_replay = source_blockchain->get_head_block_num();// std::min<unsigned>(source_blockchain->get_head_block_num(), 30000);
  BOOST_TEST_MESSAGE("Will be benchmarking " << total_blocks_to_replay << " blocks");
  BOOST_TEST_MESSAGE("First, loading them all into memory...");
  fc::time_point load_start_time(fc::time_point::now());
  std::vector<bts::client::block_message> all_messages;
  all_messages.reserve(total_blocks_to_replay);
  for (uint32_t i = 1; i <= total_blocks_to_replay; ++i)
    all_messages.emplace_back(source_blockchain->get_block(i));
  fc::time_point load_end_time(fc::time_point::now());
  BOOST_TEST_MESSAGE("Done loading into memory in " << ((double)(load_end_time - load_start_time).count() / fc::seconds(1).count()) << " seconds, replaying now...");

//#define COLLECT_DETAILED_STATS
#ifdef COLLECT_DETAILED_STATS
  std::ofstream csv_file("replay_chain_database.csv");
  csv_file << "\"block_num\",\"elapsed_time\"";
  fc::variant_object stats = client->get_chain()->get_stats();
  for (fc::variant_object::iterator iter = stats.begin(); iter != stats.end(); ++iter)
    csv_file << ",\"" << iter->key() << "\"";
  csv_file << "\n";
#endif

  fc::time_point start_time(fc::time_point::now());
  fc::microseconds overhead_time;
  client->sync_status(bts::client::block_message::type, total_blocks_to_replay);
  for (unsigned block_num = 1; block_num <= total_blocks_to_replay; ++block_num)
  {
    client->handle_message(all_messages[block_num - 1], true);

#ifdef COLLECT_DETAILED_STATS
    if (block_num % granularity == 0 || block_num == total_blocks_to_replay)
    {
      fc::time_point start_dumping_time(fc::time_point::now());
      fc::microseconds elapsed_since_start = start_dumping_time - start_time - overhead_time;
      csv_file << block_num << "," << elapsed_since_start.count();
      stats = client->get_chain()->get_stats();
      for (fc::variant_object::iterator iter = stats.begin(); iter != stats.end(); ++iter)
        csv_file << ","<< iter->value().as<size_t>();
      csv_file << "\n";

      fc::time_point end_dumping_time(fc::time_point::now());
      overhead_time += end_dumping_time - start_dumping_time;
    }
#endif

    if (block_num % 100 == 0)
      fc::yield();
  }

  client->sync_status(bts::client::block_message::type, 0);
  fc::time_point end_time(fc::time_point::now());
  BOOST_TEST_MESSAGE("Processed " << total_blocks_to_replay << " blocks in " << ((end_time - start_time - overhead_time).count() / fc::seconds(1).count()) << " seconds, " <<
                     "which is " << (((double)total_blocks_to_replay*fc::seconds(1).count()) / (end_time - start_time - overhead_time).count()) << " blocks/sec");
  client->stop();
  client_done.wait();
}

void replay_chain_database_in_stages()
{
  // now reset and try doing it in n stages
  const unsigned number_of_stages = 80;
  fc::temp_directory client_dir;
  fc::remove_all(client_dir.path());
  fc::create_directories(client_dir.path());
  fc::microseconds accumulated_time;

  bts::blockchain::chain_database_ptr source_blockchain = std::make_shared<bts::blockchain::chain_database>();
  fc::path test_net_chain_dir("C:\\Users\\Administrator\\AppData\\Roaming\\BitShares X");
  source_blockchain->open(test_net_chain_dir / "chain", fc::optional<fc::path>());
  BOOST_TEST_MESSAGE("Opened source blockchain containing " << source_blockchain->get_head_block_num() << " blocks");
  unsigned total_blocks_to_replay = source_blockchain->get_head_block_num();// std::min<unsigned>(source_blockchain->get_head_block_num(), 30000);
  BOOST_TEST_MESSAGE("Will be benchmarking " << total_blocks_to_replay << " blocks");
  BOOST_TEST_MESSAGE("First, loading them all into memory...");
  std::vector<bts::client::block_message> all_messages;
  all_messages.reserve(total_blocks_to_replay);
  for (uint32_t i = 1; i <= total_blocks_to_replay; ++i)
    all_messages.emplace_back(source_blockchain->get_block(i));
  BOOST_TEST_MESSAGE("Done loading into memory, replaying now...");

  bts::net::simulated_network_ptr sim_network = std::make_shared<bts::net::simulated_network>("wallet_tests");

  for (int n = 0; n < number_of_stages; ++n)
  {
    bts::client::client_ptr client = std::make_shared<bts::client::client>("wallet_tests", sim_network);
    std::string client_dir_string = client_dir.path().string();
    char *fake_argv[] = {"bitshares_client", "--data-dir", (char*)client_dir_string.c_str(), "--accept-incoming-connections=false", "--disable-default-peers", "--upnp=false", "--daemon", "--rpcuser=none", "--rpcpassword=none"};
    client->configure_from_command_line(sizeof(fake_argv) / sizeof(fake_argv[0]), fake_argv);
    fc::future<void> client_done = client->start();

    fc::time_point start_time(fc::time_point::now());
    unsigned start_block_num = client->get_chain()->get_head_block_num() + 1;
    unsigned end_block_num = std::min<unsigned>(source_blockchain->get_head_block_num(), start_block_num + total_blocks_to_replay / number_of_stages);

    client->sync_status(bts::client::block_message::type, end_block_num - start_block_num);
    for (unsigned block_num = start_block_num; block_num <= end_block_num; ++block_num)
    {
      client->handle_message(all_messages[block_num - 1], true);
      if (block_num % 100 == 0)
        fc::yield();
    }
    client->sync_status(bts::client::block_message::type, 0);
    fc::time_point end_time(fc::time_point::now());
    accumulated_time += end_time - start_time;
    BOOST_TEST_MESSAGE("Processed " << end_block_num - start_block_num << " blocks in " << ((end_time - start_time).count() / fc::seconds(1).count()) << " seconds, which is " << (((double)(end_block_num - start_block_num)*fc::seconds(1).count()) / (end_time - start_time).count()) << " blocks/sec");
    BOOST_TEST_MESSAGE("Total accumulated is now " << accumulated_time.count() / fc::seconds(1).count() << " seconds, which is " << (((double)end_block_num*fc::seconds(1).count()) / (accumulated_time).count()) << " blocks/sec");

    client->stop();
    client_done.wait();

    client.reset();
  }

}
#endif // defined ENABLE_REPLAY_CHAIN_DATABASE_TESTS

boost::unit_test::test_suite* init_unit_test_suite( int argc, char* argv[] )
{
  boost::unit_test::framework::master_test_suite().p_name.value = "BlockchainTests2cc";

  boost::unit_test::test_suite* regression_tests_without_network = BOOST_TEST_SUITE("regression_tests_without_network");
  boost::unit_test::test_suite* regression_tests_with_network = BOOST_TEST_SUITE("regression_tests_with_network");

  //save off current working directory and change current working directory to regression_tests directory
  fc::path original_working_directory = boost::filesystem::current_path();
  fc::path regression_tests_dir = "regression_tests";
  boost::filesystem::current_path(regression_tests_dir.string());

  //for each test directory in regression_tests directory
  fc::directory_iterator end_itr; // constructs terminating position for iterator
  for (fc::directory_iterator directory_itr("."); directory_itr != end_itr; ++directory_itr)
    if ( fc::is_directory( *directory_itr ) && (directory_itr->filename().string()[0] != '_') )
    {
      fc::path test_dir(regression_tests_dir / *directory_itr);
      boost::unit_test::test_unit* test_without_network =
        boost::unit_test::make_test_case(boost::unit_test::callback0<>(boost::bind(&run_regression_test,
                                                                                   regression_tests_dir / directory_itr->filename(),
                                                                                   false)),
                                                                       directory_itr->filename().string());
      boost::unit_test::test_unit* test_with_network =
        boost::unit_test::make_test_case(boost::unit_test::callback0<>(boost::bind(&run_regression_test,
                                                                                   regression_tests_dir / directory_itr->filename(),
                                                                                   true)),
                                                                       directory_itr->filename().string());
      regression_tests_without_network->add(test_without_network);
      regression_tests_with_network->add(test_with_network);
    }

  //restore original directory
  boost::filesystem::current_path(original_working_directory.string());

  boost::unit_test::framework::master_test_suite().add(regression_tests_without_network);
  boost::unit_test::framework::master_test_suite().add(regression_tests_with_network);

#ifdef ENABLE_REPLAY_CHAIN_DATABASE_TESTS
  boost::unit_test::framework::master_test_suite().add(BOOST_TEST_CASE(&replay_chain_database));
  boost::unit_test::framework::master_test_suite().add(BOOST_TEST_CASE(&replay_chain_database_in_stages));
#endif
  return 0;
}
