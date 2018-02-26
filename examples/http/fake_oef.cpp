#include<iostream>
#include<fstream>
#include"http/server.hpp"
#include"http/middleware/allow_origin.hpp"
#include"http/middleware/color_log.hpp"
#include"commandline/parameter_parser.hpp"
#include"random/lfg.hpp"

#include<map>
#include<vector>
#include<set>
#include<algorithm>
#include<sstream>
using namespace fetch;
using namespace fetch::http;
using namespace fetch::commandline;


struct Transaction 
{
  int64_t amount;
  byte_array::ByteArray fromAddress; // = "830A0B9D-73EE-4001-A413-72CFCD8E91F3";
  byte_array::ByteArray notes;
  uint64_t time; // = 1519650052994;
  byte_array::ByteArray toAddress; // = "6164D5A6-A26E-43E4-BA96-A1A8787091A0";
  byte_array::ByteArray json;  
};

struct Account 
{
  int64_t balance = 0;
  std::vector< Transaction > history;
};
  

class FakeOEF : public fetch::http::HTTPModule 
{
  fetch::random::LaggedFibonacciGenerator<> lfg_;
public:
  FakeOEF() 
  {

    HTTPModule::Post("/check", [this](ViewParameters const &params, HTTPRequest const &req) {
          return this->CheckUser(params, req);          
        });
    HTTPModule::Post("/register", [this](ViewParameters const &params, HTTPRequest const &req) {
          return this->RegisterUser(params, req);          
        });
    HTTPModule::Post("/balance", [this](ViewParameters const &params, HTTPRequest const &req) {
          return this->GetBalance(params, req);          
        });
    HTTPModule::Post("/send", [this](ViewParameters const &params, HTTPRequest const &req) {
          return this->SendTransaction(params, req);          
        });
    HTTPModule::Post("/get-transactions", [this](ViewParameters const &params, HTTPRequest const &req) {
        return this->GetHistory(params, req);          
      });
  }

  HTTPResponse CheckUser(ViewParameters const &params, HTTPRequest const &req) {    
    json::JSONDocument doc;
    try {      
      doc = req.JSON();
    } catch(...) {
      std::cout << req.body() << std::endl;
      
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"problems with parsing JSON\"}");     
    }
    
    if(users_.find( doc["address"].as_byte_array() ) == users_.end())
      return HTTPResponse("{\"response\": \"no\"}");
    return HTTPResponse("{\"response\": \"yes\"}");     
  }

  HTTPResponse RegisterUser(ViewParameters const &params, HTTPRequest const &req) {
    json::JSONDocument doc;
    try {      
      doc = req.JSON();
    } catch(...) {
      std::cout << req.body() << std::endl;
      
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"problems with parsing JSON\"}");     
    }

    if(users_.find( doc["address"].as_byte_array() ) != users_.end())
      return HTTPResponse("{\"response\": \"no\"}");
    
    users_.insert( doc["address"].as_byte_array() );    
    accounts_[ doc["address"].as_byte_array()  ].balance = 300 + (lfg_() % 9700);
    
    return HTTPResponse("{}"); 
  }

  HTTPResponse GetBalance(ViewParameters const &params, HTTPRequest const &req) {
    json::JSONDocument doc;
    try {      
      doc = req.JSON();
    } catch(...) {
      std::cout << req.body() << std::endl;
      
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"problems with parsing JSON\"}");     
    }

    script::Variant result = script::Variant::Object();

    if(users_.find( doc["address"].as_byte_array() ) == users_.end())
      return HTTPResponse("{\"balance\": 0}");    

    result["balance"] = accounts_[ doc["address"].as_byte_array() ].balance;
        
    std::stringstream ret;
    ret << result;    
    return HTTPResponse(ret.str());    
  }

  HTTPResponse SendTransaction(ViewParameters const &params, HTTPRequest const &req) {
    json::JSONDocument doc;
    try {      
      doc = req.JSON();
    } catch(...) {
      std::cout << req.body() << std::endl;
      
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"problems with parsing JSON\"}");     
    }

    Transaction tx;

    tx.fromAddress = doc["fromAddress"].as_byte_array();
    tx.amount = doc["balance"].as_int();
    tx.notes = doc["notes"].as_byte_array();
    tx.time = doc["time"].as_int();    
    tx.toAddress = doc["toAddress"].as_byte_array();    
    tx.json = req.body();    

    if(users_.find( tx.fromAddress ) == users_.end())
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"fromAddress does not exist\"}");

    if(users_.find( tx.toAddress ) == users_.end())
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"toAddress does not exist\"}");    

    
    if(accounts_.find(tx.fromAddress) == accounts_.end())
    {
      accounts_[tx.fromAddress].balance = 0;      
    }

    if(accounts_[tx.fromAddress].balance < tx.amount)
    {
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"insufficient funds\"}");
    }

    accounts_[tx.fromAddress].balance -= tx.amount;
    accounts_[tx.toAddress].balance += tx.amount;
    
    accounts_[tx.fromAddress].history.push_back(tx);
    accounts_[tx.toAddress].history.push_back(tx);
    
    return HTTPResponse("{\"response\": \"yes\"}"); 
  }

  HTTPResponse GetHistory(ViewParameters const &params, HTTPRequest const &req) {
    json::JSONDocument doc;
    try {      
      doc = req.JSON();
    } catch(...) {
      std::cout << req.body() << std::endl;
      
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"problems with parsing JSON\"}");     
    }
    
    auto address =  doc["address"].as_byte_array();
    if(users_.find( address ) == users_.end())
      return HTTPResponse("{\"response\": \"no\", \"reason\": \"toAddress does not exist\"}");    


    auto &account = accounts_[address];
    
    std::size_t n = std::min(20, int(account.history.size()) );
    
    script::Variant result = script::Variant::Object();
    script::Variant history = script::Variant::Array(n);
    
    for(std::size_t i=0; i < n; ++i)
    {
      history[i] = account.history[ account.history.size() - 1 - i].json;
    }

    result["data"] = history;    
    result["response"] = "yes";
    
    std::stringstream ret;
    ret << result;    
    return HTTPResponse(ret.str());        
  }
  
  
  
public:
  std::vector< Transaction > transactions_;
  std::map< fetch::byte_array::BasicByteArray, Account > accounts_;
  std::set< fetch::byte_array::BasicByteArray > users_;
  
};


int main(int argc, char const **argv) 
{

  ParamsParser params;
  params.Parse(argc, argv);

  fetch::network::ThreadManager tm(8);  
  HTTPServer http_server(8080, &tm);
  FakeOEF oef_http_interface;  
  
  http_server.AddMiddleware( fetch::http::middleware::AllowOrigin("*") );       
  http_server.AddMiddleware( fetch::http::middleware::ColorLog);
  http_server.AddModule(oef_http_interface);

  tm.Start();
  
  std::cout << "Ctrl-C to stop" << std::endl;
  while(true) 
  {
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
  }

  tm.Stop();
  
  return 0;
}
