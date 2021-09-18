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

	return in.split();
}

vector<double> getPrice()
{
	vector<double> prices(2);
	string text = transacation("GET", "/api/v3/ticker/bookTicker?symbol=GALAUSDT");
	cout << text << endl;
//	auto root = json::api.decode(text);
//	json::api.get(root, "bidPrice", prices[0]);
//	json::api.get(root, "askPrice", prices[1]);
	return prices;
}

string buy(const string &coin, double price, int number)
{
	map<string, string> attribute;
	attribute["Content-Type"] = "application/x-www-form-urlencoded";
	attribute["X-MBX-APIKEY"] = apiKey;
	http::QueryString qs;
	qs.setParameter("symbol", coin);
	qs.setParameter("side", "BUY");
	qs.setParameter("type", "LIMIT");
	qs.setParameter("timeInForce", "GTC");
	qs.setParameter("quantity", to_string(number));
	qs.setParameter("price", to_string(price));
	qs.setParameter("timestamp", to_string(time(NULL)));
	qs.setParameter("recvWindow", to_string(5000));
	string body = qs.toString();
	ls::SHA256 sha256;
	string signature = sha256.hmac(body, secretKey);
	body.append("&signature=");
	body.append(signature);
	return transacation("POST", "/api/v3/order", body, attribute);
}

int main(int argc, char **argv)
{
	ip = argv[1];
	url = argv[2];
	apiKey = argv[3];
	secretKey = argv[4];
	getPrice();
	cout << buy("GALAUSDT", 0.08, 200) << endl;
}
