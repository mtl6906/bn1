#include "ls/http/Request.h"
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
#include "unistd.h"

using namespace ls;
using namespace std;

char *ip, *url, *secretKey, *apiKey;

string transacation(const string &method, const string &url, const string &body = "", const map<string, string> &attributes = map<string, string>())
{
	http::Request request;
	request.setDefaultHeader();
	request.getMethod() = method;
	request.getURL() = url;
	request.getBody() = body;
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
	
	io::OutputStream out(connection -> getWriter(), new Buffer());
	string text = request.toString();
	
	LOGGER(ls::INFO) << "request:\n" << text << ls::endl;
	
	out.append(text);
	out.write();

	LOGGER(ls::INFO) << "cmd sending..." << ls::endl;

	io::InputStream in(connection -> getReader(), new Buffer());
	in.read();

	LOGGER(ls::INFO) << "reading..." << ls::endl;

	in.split("\r\n\r\n", true);
	return in.split();
}

vector<double> getPrice()
{
	vector<double> prices(2);
	string text = transacation("GET", "/api/v3/ticker/bookTicker?symbol=GALAUSDT");
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
	attribute["Content-Type"] = "application/x-www-form-urlencoded";
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
	return order(coin, price, number, "BUY");
}

string sell(const string &coin, double price, double number)
{
	return order(coin, price, number, "SELL");
}

int getBuyOrderNumber(const string &coin)
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

	json::Array array;
	array.parseFrom(responseText);
	for(int i=0;i<array.size();++i)
	{
		json::Object it;
		string type;
		json::api.get(array, i, it);
		json::api.get(it, "side", type);
		if(type == "BUY");
			++count;
	}
	return count;
}

void method()
{
	int orderNumber = 0;
	double signPrice = 0;
	double signPriceBefore = 0;
	double rate = 0.997;
	for(;;)
	{
		sleep(1);
		auto prices = getPrice();
		auto buyOrderNumber = getBuyOrderNumber("AVAXUSDT");
		if(buyOrderNumber < orderNumber)
		{
			--orderNumber;
			signPrice = signPriceBefore;
		}
		if(orderNumber == 0)
		{
			sell("AVAXUSDT", prices[0], 0.2);
			buy("AVAXUSDT", prices[0] * 0.997, 0.2);
			signPrice = prices[0];
			orderNumber++;
		}
		else
		{
			if(orderNumber >= 5)
				continue;
			long long currentPrice = (long long)(prices[0] * 10000);
			long long signPriceNow = (long long)(signPrice * 1.005 * 10000);
			if(currentPrice > signPriceNow)
			{
				sell("AVAXUSDT", prices[0], 0.2);
				buy("AVAXUSDT", prices[0] * 0.997, 0.2);
				signPriceBefore = signPrice;
				signPrice = prices[0];
				orderNumber++;
			}
		}
	}
}



int main(int argc, char **argv)
{
	ip = argv[1];      
	url = argv[2];
	apiKey = argv[3];
	secretKey = argv[4];
//	getPrice();
//	cout << buy("GALAUSDT", 0.08, 200) << endl;
//	cout << sell("ARUSDT", 90, 0.3) << endl;
//	cout << getBuyOrderNumber("GALAUSDT") << endl;
	method();
}
