#include "ls/http/Request.h"
#include "ls/http/StringBody.h"
#include "ls/http/Response.h"
#include "ls/http/QueryString.h"
#include "ls/io/InputStream.h"
#include "ls/io/OutputStream.h"
#include "ls/ssl/Client.h"
#include "ls/net/Client.h"
#include "ls/json/API.h"
#include "ls/SHA256.h"
#include "ls/DefaultLogger.h"
#include "string"
#include "vector"
#include "iostream"
#include "memory"
#include "stack"
#include "unistd.h"
#include "thread"

using namespace ls;
using namespace std;

char *ip, *url, *secretKey, *apiKey, *coinname;
double rate, uprate, coinnumber;

io::InputStream in(nullptr, new Buffer());
io::OutputStream out(nullptr, new Buffer());

std::string signature(const vector<string> &v)
{
	string signaturePayload;
	for(auto &it : v)
		signaturePayload += it;
	ls::SHA256 sha256;
	return sha256.hmac(signaturePayload, secretKey);				
}

string transacation(const string &method, const string &url, const string &body = "", const map<string, string> &attributes = map<string, string>())
{
	http::Request request;
	request.setDefaultHeader();
	request.getMethod() = method;
	request.getURL() = url;
	request.setBody(new http::StringBody(body, "application/x-www-form-urlencoded"));
	request.getVersion() = "HTTP/1.1";
	request.setAttribute("Host", "api.binance.com");
	request.setAttribute("User-Agent", "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:89.0) Gecko/20100101 Firefox/89.0");
	if(body != "")
		request.setAttribute("Content-Length", to_string(body.size()));
	for(auto &attribute : attributes)
		request.setAttribute(attribute.first, attribute.second);
	ssl::Client sslClient;
	unique_ptr<ssl::Connection> connection(sslClient.getConnection(net::Client(ip, 443).connect()));
	connection -> setHostname(url);
	connection -> connect();
	
	out.reset(connection -> getWriter());
	string text = request.toString();
	
	LOGGER(ls::INFO) << "request:\n" << text << ls::endl;
	
	out.append(text);
	out.append(body);
	out.write();

	LOGGER(ls::INFO) << "cmd sending..." << ls::endl;

	in.reset(connection -> getReader());

	http::Response response;
	string result;
	for(;;)
	{
		in.read();
		LOGGER(ls::INFO) << "reading..." << ls::endl;
		try
		{
			if(response.getCode() == "")
			{
				auto text = in.split("\r\n\r\n", true);
				response.parseFrom(text);
				LOGGER(ls::INFO) << text << ls::endl;
			}
			int contentLength = stoi(response.getAttribute("Content-Length"));
			result = in.split(contentLength);
			LOGGER(ls::INFO) << text << ls::endl;
		}
		catch(Exception &e)
		{
			sleep(1);
			continue;
		}
		break;
	}
	return result;
}

void cancelAllOrders(const string &coin)
{
	http::QueryString qs;
	qs.setParameter("symbol", coin);
	qs.setParameter("timestamp", to_string(time(NULL) * 1000));
	qs.setParameter("recvWindow", to_string(5000));
	map<string, string> attribute;
	attribute["X-MBX-APIKEY"] = apiKey;
	ls::SHA256 sha256;
	string body = qs.toString();
	string signature = sha256.hmac(body, secretKey);
	body += "&signature=" + signature;
	auto responseText = transacation("DELETE", "/api/v3/openOrders?" + body, "", attribute);
	cout << responseText << endl;
}

vector<double> getPrice(const string &coin)
{
	vector<double> prices(2);
	string text = transacation("GET", string("/api/v3/ticker/bookTicker?symbol=") + coin);
	cout << text << endl;
	auto root = json::api.decode(text);
	string price;
	json::api.get(root, "bidPrice", price);
	prices[0] = stod(price);
	json::api.get(root, "askPrice", price);
	prices[1] = stod(price);
	return prices;
}

string order(const string &coin, double price, double number, const string &type)
{
	map<string, string> attribute;
	attribute["X-MBX-APIKEY"] = apiKey;
	http::QueryString qs;
	qs.setParameter("symbol", coin);
	qs.setParameter("side", type);
	qs.setParameter("type", "LIMIT");
	qs.setParameter("timeInForce", "GTC");
	qs.setParameter("quantity", to_string(number));
	qs.setParameter("price", to_string(price));
	qs.setParameter("timestamp", to_string(time(NULL)*1000));
	qs.setParameter("recvWindow", to_string(5000));
	string body = qs.toString();
	ls::SHA256 sha256;
	string signature = sha256.hmac(body, secretKey);
	body.append("&signature=");
	body.append(signature);
	return transacation("POST", "/api/v3/order", body, attribute);
}

string buy(const string &coin, double price, double number)
{
	auto text = order(coin, price, number, "BUY");
	LOGGER(ls::INFO) << text << ls::endl;
	return text;
}

string sell(const string &coin, double price, double number)
{
	auto text = order(coin, price, number, "SELL");
	LOGGER(ls::INFO) << text << ls::endl;
	return text;
}

double round2(double value)
{
	int v = value * 100;
	return v / 100.0;
}

json::Array getOrders(const string &coin)
{
	map<string, string> attribute;
	attribute["X-MBX-APIKEY"] = apiKey;
	string url = "/api/v3/openOrders?";
	http::QueryString qs;
	string ts = to_string(time(NULL) * 1000);
	qs.setParameter("symbol", coin);
	qs.setParameter("recvWindow", "5000");
	qs.setParameter("timestamp", ts);
	qs.setParameter("signature", signature({qs.toString()}));
	url += qs.toString();

	auto responseText = transacation("GET", url, "", attribute);
	json::Array orders;
	orders.parseFrom(responseText);
	return orders;
}

pair<int, double> getBuyOrderNumberAndMax(const string &coin)
{
	map<string, string> attribute;
	attribute["X-MBX-APIKEY"] = apiKey;
	int count = 0;
	http::QueryString qs;
	qs.setParameter("symbol", coin);
	qs.setParameter("recvWindow", "5000");
	qs.setParameter("timestamp", to_string(time(NULL) * 1000));
	string url = "/api/v3/openOrders?";
	auto text = qs.toString();
	url += text + "&signature=";
	ls::SHA256 sha256;
	url += sha256.hmac(text, secretKey);
	auto responseText = transacation("GET", url, "", attribute);

	LOGGER(ls::INFO) << responseText << ls::endl;

	double maxPrice = 0;
	json::Array array;
	array.parseFrom(responseText);
	for(int i=0;i<array.size();++i)
	{
		json::Object it;
		string type;
		json::api.get(array, i, it);
		json::api.get(it, "side", type);
		if(type == "BUY")
		{
			++count;
			string text;
			json::api.get(it, "price", text);
			double price = stod(text);
			int p1 = (int)(maxPrice * 100), p2 = (int)(price * 100);
			if(p1 < p2)
				maxPrice = price;
		}
	}
	pair<int, double> result;
	result.first = count;
	result.second = maxPrice;
	return result;
}


void method(const string &coin, double number)
{
	for(;;)
	{
		sleep(2);
		auto prices = getPrice(coin);
		auto buyOrderNumber = getBuyOrderNumberAndMax(coin);
		if(buyOrderNumber.first == 0)
		{
			buy(coin, round2(prices[1]), number);
			sell(coin, prices[0], number);
		}
		else
		{
			if(buyOrderNumber.first >= 5)
				continue;
			long long currentPrice = (long long)(prices[0] * 10000);
			long long signPriceNow = (long long)(buyOrderNumber.second * (1 + uprate) * 10000);
			if(currentPrice > signPriceNow)
			{
				sell(coin, prices[0], number);
				buy(coin, round2(prices[0] * (1 - rate)), number);
			}
		}
	}
}


/*

int main(int argc, char **argv)
{
	ip = argv[1];      
	url = argv[2];
	apiKey = argv[3];
	secretKey = argv[4];
	rate = stod(argv[5]);
	uprate = stod(argv[6]);
	coinname = argv[7];
	coinnumber = stod(argv[8]);
	LOGGER(ls::INFO) << "rate: " << rate << ls::endl;
//	getPrice();
//	cout << buy("GALAUSDT", 0.08, 200) << endl;
//	cout << sell("ARUSDT", 90, 0.3) << endl;
//	cout << getBuyOrderNumber("GALAUSDT") << endl;
//	method(coinname, coinnumber);
	auto orders = getOrders("AVAXUSDT");
	cout << orders.size() << endl;
}

*/

void method1()
{
	for(;;)
	{
		int ch;
		cout << "1. order" << endl;
		cout << "2. cancel" << endl;
		cin >> ch;
		if(ch == 1)
		{
			auto result = getPrice("BTCBUSD");	
			buy("BTCBUSD", result[0], 0.00111);
			sell("BTCBUSD", result[0] + 1, 0.00111);
		}
		else if(ch == 2)
		{
			cancelAllOrders("BTCBUSD");
		}
	}
}

double buyprice = 0;

void method2()
{
	for(;;)
	{
		auto result = getPrice("BTCBUSD");	
		auto text = buy("BTCBUSD", result[0], 0.00113);
		auto root = json::api.decode(text);
		string status;
		try
		{
			json::api.get(root, "status", status);
			buyprice = result[0];
		}
		catch(Exception &e)
		{
		
		}

		while(buyprice > 1)
		{
			auto array = getOrders("BTCBUSD");
			if(array.size() == 0)
			{	
				sell("BTCBUSD", buyprice + 3, 0.00113);
				buyprice = 0;
				break;
			}
		}
		sleep(5);
	}
}

void method4()
{
	for(;;)
	{
		auto result = getPrice("BTCBUSD");
		buy("BTCBUSD", result[0], 0.00113);
		sell("BTCBUSD", result[0] + 5, 0.00113);
		json::Array array;
		do
		{
			sleep(2);
			array = getOrders("BTCBUSD");
		}
		while(array.size() > 0);
	}
}

void cancelT()
{
	for(;;)
	{
		int c;
		cin >> c;
	       	if(c == 1)	
		{
			buyprice = 0;
			cancelAllOrders("BTCBUSD");
		}
	}
}

int main(int argc, char **argv)
{
	ip = argv[1];
	url = argv[2];
	apiKey = argv[3];
	secretKey = argv[4];
for(;;)
{
	try 
	{
		method4();
	}
	catch(Exception &e)
	{
		continue;
	}
}
//	cout << result[0] << " " << result[1] << endl;

	return 0;
}

