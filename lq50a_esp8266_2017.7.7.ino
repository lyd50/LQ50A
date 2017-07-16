//硬件：nodemcu  ds18b20 崂泉50A 废水水流传感器 超滤冲洗传感器
//2017.5.23 早上加入上传WiFi信号强度功能
//崂泉20A 水流传感器串接废水与超滤冲洗公共管路，可以检测超滤电磁阀不能打开，不能关闭，超滤堵塞
//加常开净水电磁阀，制水时间超过一小时关闭净水输出
//eeprom 记忆参数
//2017.7.2 加入浏览器更新固件功能   http://webupdate.local/update
//2017.7.5加入服务器更新程序功能  http://lyd50.vicp.io:81/20a.bin

#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiAP.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Arduino.h>
#include <EEPROM.h>//掉电保存
#include <ArduinoJson.h>
#include    "EdpKit.h"
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266httpUpdate.h>
const char* host = "webupdate";

ESP8266WebServer updateServer(80);
ESP8266HTTPUpdateServer httpUpdater;
ESP8266WiFiMulti WiFiMulti;
/* ---------------------------------------全局设置开始-----------------------------------------*/
bool ji_qi_shi_yan = false;//调试时设为真，工作状态为假
EdpPacketClass edp;
//DynamicJsonBuffer jsonBuffer;
String state = "qidong";//初始状态
bool kaiguan = true;//是否运行净水器工作,true时允许

					/* ---------------------------------------全局设置结束-----------------------------------------*/
					/* ---------------------------------------edp有关设置-----------------------------------------*/
bool wifi_connected = false;
unsigned int version = 20170707;
bool chxu_update = false;
const char* edp_server = "jjfaedp.hedevice.com";
unsigned int edp_port = 876;
bool   edpConnected = false;
String apikey = "", device_id = "";
unsigned char buffer[200];     //接收缓存 
unsigned long ht__wangluo_dingshi;//心跳定时发出
unsigned long kaiguan_dingshi;//是否允许净水器运行信号定时发出
							  /* ---------------------------------------edp有关设置结束-----------------------------------------*/
							  /*-----------------------------------------http有关设置--------------------------------------*/
const char*  http_server = "api.heclouds.com";
unsigned  int http_port = 80;
/*-----------------------------------------http有关设置结束--------------------------------------*/

/*-----------------------------------------芯片自设web服务器有关设置开始--------------------------------------*/
IPAddress apIP(192, 168, 217,1),ap_gate(192,168,217,1), wifi_ip(0, 0, 0, 0);    //AP 网络服务器  ip address
char* ap_ssid = "nodemcu";
char*  ap_pass = "12345678";//设定的密码
ESP8266WebServer  server(apIP, 80);
String html = "", josn_string = "";

/* ---------------------------------制水有关设置开始-----------------------------------------------*/

//bool ximo_baojing = true;
bool xu_yao_ximo = false;//洗过膜后置为假，制水时置真
//bool xi_guo_mo = false;
bool queshui_1_1 = false,queshui_1_2=false;
bool zhishui_1_1 = false,zhishui_1_2=false;
unsigned char minute;//单次制水时间
unsigned char  ximo_cishu = 0;
unsigned char  nongshui = 19;//制水时浓水标准值。关键数值，必须设默认值，防止读取失败
unsigned char  ximo_sudu = 5;//第10次洗膜采样的标准值，必须设默认值，防止读取失败
unsigned char yalitong_shuiman_fenzhong = 5;//单位分钟，关键数值，必须设默认值
unsigned  char zhishui_xiao_gu_zhang = 0;//废水太少时查数，保护用
unsigned char ximo_guzhang = 0;//净水洗膜时，废水流速过少计次
unsigned long  begintime_zhishui , endtime_zhishui , time_local, time_local1, time_local2;
unsigned long jiancha_chaolv;
volatile unsigned int count = 0;//废水传感器开关次数
volatile unsigned int chlv_count = 0;//超滤传感器开关次数
unsigned char zhishui[10], ximo[40];
unsigned long queshui_baojing_dingshi;
//JsonObject& root = jsonBuffer.createObject();
unsigned int i2 = 0;//超滤冲洗，制水时显示水速和洗膜时显示浓水水速索引
String worktime_h = "", worktime_m = ""; char string[20];

/*----------------------------------------WiFi有关设置-----------------------------------------------------*/

//Ticker t_check_net;//定期检查WiFi和edp连接情况
//Ticker t_edp_heartbeat;//edp心跳请求
int wifi_disconnect_count = 0, edp_disconnect_count = 0;
WiFiClient edp_client;//

WiFiClient update_client;//程序更新
String	former_wifi_ssid = "", wifi_ssid = "", former_wifi_pass = "", wifi_pass = "";
/*----------------------------------------日志有关设置-----------------------------------------------------*/
String zhangtai[10];
unsigned int zhuang_tai_index = 0;
/*----------------------------------------温度有关设置-----------------------------------------------------*/
OneWire oneWire(D4);//一线测温
DallasTemperature sensors(&oneWire);
// arrays to hold device address
DeviceAddress insideThermometer;
unsigned long wendu_dingshi = millis();

/*--------------------------------------------超滤冲洗----------------------------------------------------*/
unsigned char chaolv_chxi[15];//记录超滤冲洗
unsigned  char chlv_chxi_gu_zhang = 0;//超滤冲洗时水量太小时查数，保护用
unsigned  char chlv_tingji_gu_zhang = 0;//停机时检测超滤是否有水流，保护用
unsigned char chaolv_chxi_biaozhun = 50;
 
void apStart()
{  
	WiFi.mode(WIFI_AP_STA);

	//WiFi.softAPIP.begin();
	if (WiFi.softAP(ap_ssid, ap_pass, 1, 0)&&WiFi.softAPConfig(apIP, ap_gate, IPAddress(255, 255, 255, 0)))
	{
		Serial.print("http服务器:"); Serial.println(apIP);
		Serial.print("连接到：");
		Serial.println(ap_ssid);
	}
	

}
void ap_root() {

	html = "<!DOCTYPE html>	<html lang = 'ch' xmlns = 'http://www.w3.org/1999/xhtml'><head>	<meta charset = 'utf-8' />";
	html += "<title>净水器状态显示</title>";
	html += "</head><body style = 'text-align:center;font-size:50px'>";

	html += "<h1>净水器状态:</h1><form>";
	for (int w = 0; w < 10; w++)//净水器状态倒序读出
	{
		if (zhangtai[w] != "") {
			html += "<label>";
			html += (String)(w + 1) + " :";
			html += zhangtai[w]; html += "</label><br />";
		}
	}

	html += "<label>超滤冲洗时水速：";
	for (int z = 0; z < 15; z++) {
		html += chaolv_chxi[z];
		html += "   ";
	}
	html += "</label><br /><br />";


	html += "<label>制水时浓水速：";
	for (int z = 0; z < 10; z++) {
		html += zhishui[z];
		html += "   ";
	}
	html += "</label><br /><br />";
	html += "<label>洗膜时浓水速：";

	for (int w = 0; w < 40; w++)
	{
		html += ximo[w];
		html += "   ";
	}
	html += "</label><br /><br />";
	html += "<label>单次制水时间:" + (String)minute + " 分钟</label><br />";
	html += "<label>制水总工作时间:" + worktime_h + " 小时，" + worktime_m + " 分钟。</label><br />";
	html += "<label>制水时浓水报警转速: " + (String)nongshui + "</label><br />";
	html += "<label>洗膜时浓水报警转速: " + (String)ximo_sudu + "</label><br />";
	html += "<label>压力桶失效检测时间分钟数: " + (String)yalitong_shuiman_fenzhong + "</label><br />";
	html += "<label>WiFi分配的网址:" + wifi_ip.toString() + "</label><br />";
	if (kaiguan == true) { html += "<label>净水器处于允许运行状态</label><br />"; }
	else { html += "<label>净水器关闭状态</label><br />"; }
	html += "<h3>售后电话:17737203569</h3><h3>QQ:16635265  微信:ly_lyd50</h3></form></body></html>";
	server.send(200, "text/html", html);
	html = "";

}

void ap_handle_para() 
{
	int gaidong = 0;
	if (server.args() >= 1) 
	{

		if (server.arg("kaiguan") == "1")
		{
			if (kaiguan != true) { kaiguan = true; EEPROM.write(403, 1); gaidong=gaidong+1;
			
			edp_upload_string("工作状态", "净水器开机");
			
			}


		}
	}
		else if (server.arg("kaiguan") == "0")
		{
			if (kaiguan != false)
			{
				kaiguan = false; EEPROM.write(403, 0); gaidong=gaidong+1;
				Serial.println("净水器关闭");
				edp_upload_string("工作状态", "净水器关机");
				kaiguan_dingshi = kaiguan_dingshi - 300000;//立即执行关闭净水器
			}

		}

		if (server.arg("ssid") != ""&&wifi_ssid != server.arg("ssid")) {
			
				wifi_ssid = server.arg("ssid");
				for (int i = 0; i < 15; i++) { EEPROM.write(360 + i, 0); }

				for (int i = 0; i < wifi_ssid.length(); i++)//360-370 wifi_ssid，15个
				{
					EEPROM.write(360 + i, wifi_ssid.charAt(i)); gaidong=gaidong+1;
				}	
				edp_upload_string("wifi", "wifi的SSID："+wifi_ssid+"");

		}

		if (server.arg("pass") != ""&&wifi_pass != server.arg("pass"))
		{
			wifi_pass = server.arg("pass");
			edp_upload_string("wifi", "wifi的密码：" + wifi_pass + "");
			for (int i = 0; i < 15; i++) { EEPROM.write(380 + i, 0); }
			for (int i = 0; i < wifi_pass.length(); i++)//380-395 wifi_pass，15个
			{
				EEPROM.write(380 + i, wifi_pass.charAt(i)); gaidong=gaidong+1;
			}
		}

		if (server.arg("device_id") != "" && device_id != server.arg("device_id"))
		{ 
			device_id = server.arg("device_id");
			for (int i = 0; i < 10; i++) { EEPROM.write(340 + i, 0); }
			for (int i = 0; i < device_id.length(); i++)//340-350 device_id,10个
			{
				EEPROM.write(340 + i, device_id.charAt(i)); gaidong=gaidong+1;
			}		
		}
		
		if (server.arg("nongshui") != ""&&nongshui != server.arg("nongshui").toInt()) 
		{
			nongshui = server.arg("nongshui").toInt();
			EEPROM.write(400, nongshui);//		
			gaidong=gaidong+1;
			edp_upload_string("报警标准", "更改制水时浓水值："+(String)nongshui+"");
			Serial.print("更改制水时浓水值："); Serial.println(nongshui);

	    }

		if (server.arg("yalitongshuimanfenzhong") != ""&&yalitong_shuiman_fenzhong != server.arg("yalitongshuimanfenzhong").toInt()) 
		{
			yalitong_shuiman_fenzhong = server.arg("yalitongshuimanfenzhong").toInt();
			EEPROM.write(401, yalitong_shuiman_fenzhong);//
			gaidong=gaidong+1;

		}
		
		if (server.arg("ximo") != ""&&ximo_sudu != server.arg("ximo").toInt()) { 
			ximo_sudu = server.arg("ximo").toInt(); EEPROM.write(402, ximo_sudu);// 
			gaidong=gaidong+1;
		}

		if (server.arg("apikey") != ""&& apikey != server.arg("apikey")) 
		{ 
			apikey = server.arg("apikey");
			//for (int i = 0; i < 28; i++) { EEPROM.write(300 + i, 0); }
			for (int i = 0; i < 28; i++)//28个,300开始
			{
				EEPROM.write(300 + i, apikey.charAt(i)); gaidong++;
			}	
		
		}
		
		

		if (wifi_ssid != "" || device_id != "" || apikey != "" || nongshui > 1 || ximo_sudu > 1 || yalitong_shuiman_fenzhong > 1)//只要一项提交成功
		{
			html = "";
			html = "<!DOCTYPE html><html lang = \"ch\" xmlns = \"http://www.w3.org/1999/xhtml\"><head>";
			html += "<meta charset = \"utf-8\" /><title>提交反馈</title>";
			html += "<style>input{color: red;font-size: 50px;text-align: center} </style></head>";
			html += "<body style = \"text-align:center;font-size:50px\"><h1>提交反馈<BR></h1>";
           if (gaidong >= 1) {
              html+="<h2>提交成功，净水器重起</h2></body></html>";
			Serial.println("执行eeprom内容更改");
			EEPROM.commit();server.send(200, "text/html", html);
			ESP.restart();
		     }
		   else
		   {
			   html += "<h2>提交成功，数据没有改变，净水器不重起</h2></body></html>";
			   server.send(200, "text/html", html);
		   }

			
			
			//fs_write();
		}
		else
		{
			html = "";
			html = "<!DOCTYPE html><html lang =\"ch\" xmlns = \"http://www.w3.org/1999/xhtml\"><head>";
			html += "<meta charset=\"utf-8\"/><title>提交反馈</title>";
			html += "<style>input{color: red;font-size: 50px;text-align: center} </style></head>";
			html += "<body style=\"text-align:center;font-size:50px\"><h1>提交反馈<BR></h1><h2>提交失败,至少一项不能为空，请检查后重新提交</h2></body></html>";
			server.send(200, "text/html", html);
		}    
	
}
void ap_set() {
	html = "<!DOCTYPE html><html lang =\"ch\" xmlns =\"http://www.w3.org/1999/xhtml\"><head><meta charset =\"utf-8\"/>";
	html += "<title>净水器设置</title><style>input{color: red;font-size:50px;text-align: center}</style></head>";
	html += "<body style =\"text-align:center;font-size:50px\"><h1>净水器设置, 谨慎更改<BR></h1>";
	html += "<form method ='POST' action ='/input.html'>";
	html += "<label>WiFi的SSID<input type='text' name='ssid'";
	if (wifi_ssid != "") { html += "value='" + wifi_ssid + "' "; }
	html += " placeholder='本地WiFi的名称'></label><br /><br />";
	html += "<label>WiFi的密码<input type='text' name='pass'";
	if (wifi_pass != "") { html += "value='" + wifi_pass + "' "; }
	html += " placeholder='WiFi的密码'></label><br /><br />";
	html += "<label>云设备ID<input type='text' name='device_id' value='" + device_id + "' placeholder=\"云端设备id，纯数字\"></label><br /><br />";
	html += "<label>云设备api-key <input type='text' name ='apikey' value='" + apikey + "' placeholder=\"云端设备apikey\"></label><br /><br />";
	html += "<label>浓水报警转速<input type='text' name='nongshui' value='" + (String)nongshui + "'  placeholder=\"低于此数字值便报警\"></label><br /><br />";
	html += "<label>洗膜报警转速<input type='text' name='ximo' value='" + (String)ximo_sudu + "' placeholder=\"低于此数字值便报警\"></label><br /><br />";
	html += "<label>最快压力桶水满分钟数<input type='text' value='" + (String)yalitong_shuiman_fenzhong + "' name='yalitongshuimanfenzhong' placeholder=\"低于此值报警计次一次\"></label><br /><br />";
	html += "<label>净水器开关 0或者1:<input type='text' value='" + (String)kaiguan + "' name='kaiguan'></label><br /><br />";
	html += "<input style=\"color:red;font-size:60px\" type='submit' value='提交更新'></form></body></html>";
	server.send(200, "text/html", html);
	html = "";

}
/*void  ap_handle_log() //写入芯片自身，作为不能上网时的日志
{
	if (SPIFFS.begin())
	{
		File log;
		if (SPIFFS.exists("/log.txt")) {
			log = SPIFFS.open("/log.txt", "r");

			josn_string = log.readStringUntil(']') + ']';//读取到字符串中

			log.close();//关闭文件 
			SPIFFS.end();
			//Serial.println(josn_string);
			JsonArray& arr = jsonBuffer.parseArray(josn_string);

			if (arr.success()) {
				Serial.println("日志文件解析成功");
				html = "";
				html = "<!DOCTYPE html>	<html lang = 'ch' xmlns = 'http://www.w3.org/1999/xhtml'><head>	<meta charset = 'utf-8' />";
				html += "<title>净水器日志</title>";
				html += "</head><body style = 'text-align:center;font-size:50px'>";

				html += "<h1>净水器日志:</h1><form>";

				for (int i = arr.size(); i > 0; i--)//倒序读出
				{
					html += "<label>";
					html += (String)i + " : "; html += arr[i - 1].asString(); html += "</label><br />";
					//++x;  // increment x by one and returns the new value of x
				}
				html += "<h3>售后电话:梁：17737203569 李：15637938683</h3><h3>QQ:16635265  微信:ly_lyd50</h3></form></body></html>";

				server.send(200, "text/html", html);
				Serial.println("网络客户端访问，日志文件解析成功");
			}
			else
			{
				html = "";
				html = "<!DOCTYPE html>	<html lang = 'ch' xmlns = 'http://www.w3.org/1999/xhtml'><head>	<meta charset = 'utf-8' />";
				html += "<title>净水器日志</title>";
				html += "</head><body style = 'text-align:center;font-size:50px'>";

				html += "<h1>净水器日志:</h1><form>";

				html += "<h1>日志打开后解析失败</h1>";
				html += "<h3>售后电话:梁：17737203569 李：15637938683</h3><h3>QQ:16635265  微信:ly_lyd50</h3></form></body></html>";

				server.send(200, "text/html", html);
				Serial.println("网络客户端访问，日志文件解析失败");
			}
			arr.end(); //josn 数组关闭
		}
		else {
			Serial.println("网络客户端访问，日志文件打开失败");
			SPIFFS.end();
		}
	}

	else { Serial.println("网络客户端访问，文件系统打开失败"); }
}*/

void chlv_detect(int interval)/*检测超滤水流传感器,标准次数，时间间隔,单位毫秒*/
{
	ESP.wdtFeed();
	chlv_count = 0;/*计数次数归零,重新计数*/
			  //unsigned long	  begintime = millis(),  endtime = millis();
	attachInterrupt(digitalPinToInterrupt(D1), chlv_count_function, FALLING);
	unsigned long time9 = millis() + interval;
	while (millis() < time9) {};
	//while (endtime - begintime < interval) { endtime = millis(); }
	detachInterrupt(digitalPinToInterrupt(D1));
}
void chlv_count_function() { chlv_count++; }

void detect(int interval)/*检测废水水流传感器,标准次数，时间间隔,单位毫秒*/
{
	ESP.wdtFeed();
	count = 0;/*计数次数归零,重新计数*/
			  //unsigned long	  begintime = millis(),  endtime = millis();
	attachInterrupt(digitalPinToInterrupt(D2), count_function, FALLING);
	unsigned long time9 = millis() + interval;
	while (millis() < time9) {};
	//while (endtime - begintime < interval) { endtime = millis(); }
	detachInterrupt(digitalPinToInterrupt(D2));
}
void count_function() { count++; }
void edp_check_net() //检查网络情况
{
	if (edpConnected == false)//有多种edp连接反馈，edpConnected反应真实连接情况
	{
		Serial.println("检查网络连接情况一次，edp网络没有连接");
		if (WiFi.status() != WL_CONNECTED)
		{
			wifi_set(wifi_ssid, wifi_pass);//wifi 15次连接			

		}
		if ((WiFi.status() != WL_CONNECTED))//15次连接后还没有连接成功
		{
			f_zhuang_tai("wifi连接不成功，等待200秒下次连接");
			if (ji_qi_shi_yan == true)//调试状态
			{
				ht__wangluo_dingshi = millis() + 60000; //间隔1分钟发送心跳.和检查网络情况
			}
			else//正常运行状态
			{
				ht__wangluo_dingshi = millis() + 200000; //间隔200秒分钟发送心跳.检查网络情况
			}
			wifi_disconnect_count++;
			//log_write("wifi连接不成功，等待200秒后下次连接");
			Serial.print("wifi连接不成功，等待200秒后下次连接");
		}

		else//15次后WiFi连接上了
		{

			wifi_disconnect_count = 0;//WiFi连接失败次数置零

			edp_first_connect();
			if (edpConnected == true) {
				edp_upload_string("wifi", "wifi重新连接成功");
				f_zhuang_tai("wifi重新连接成功");
				//log_write("wifi重新连接成功");
				Serial.println("wifi重新连接成功");

			}

		}
	}
	else//edp网络连接状态
	{
		Serial.println("定时检查网络一次，网络连接正常");
		f_zhuang_tai("定时检查网络正常，edp 连接成功");

		//log_write("定时检查网络正常，edp 连接成功");//日志记录

		edp_upload_int("WiFi信号强度", WiFi.RSSI());
		f_zhuang_tai("wifi信号强度：" + (String)WiFi.RSSI() + "");
		//log_write("wifi信号强度：" + (String)WiFi.RSSI() + "");

	}
}
void  edp_command_parse() //约定命令四个数字,10以内前面加零，前两个是命令类型，后两个是命令值，均非零
{
	int  order_type = (int(buffer[44]) - 48) * 10; //Serial.println(order_type);
	order_type = order_type + (int(buffer[45]) - 48);
	int  value;

	//Serial.println(value);
	switch (order_type)
	{
		/*------------------------------------主动停止和启动净水器------------------------------------------*/
	case 1://约定高压开关非接地端控制，
		value = int(buffer[46] - 48) * 10; 	//Serial.println(value);
		value = value + (int(buffer[47]) - 48);
		switch (value)
		{
		case 1://高压开关接地与接高电位切换  0101

			Serial.println("开关状态转换");
			kaiguan = !kaiguan;//要有这个赋值语句。
			if (kaiguan == true)//真值时
			{
				edp_upload_int("开关", 1);

				EEPROM.write(403,1);//写入eeprom
				kaiguan = true;
				digitalWrite(D5, LOW);//高压开关接地，正常工作	
				edp_upload_string("命令结果", "命令值有效，已经切换");
				edp_upload_string("工作状态", "开关转换至允许运行状态");
			}
			else
			{
				digitalWrite(D5, HIGH);//高压开关接高电位，停止工作	
				kaiguan = false;
				EEPROM.write(403, 0);//写入eeprom
				edp_upload_int("开关", 0);
				edp_upload_string("命令结果", "命令值有效，已经切换");
				edp_upload_string("工作状态", "开关转换至停机状态");
			}
			break;
		default:edp_upload_string("命令结果", "命令值无效");
			EEPROM.commit();

			break;

		}

		break;
		/*-------------------------------------------设定制水时浓水------------------------------------------*/
	case 2: //制水时浓水  02{V}
		value = int(buffer[46] - 48) * 10; 	//Serial.println(value);
		value = value + (int(buffer[47]) - 48);

		if (10 < value < 30) {
			nongshui = value;
			EEPROM.write(400, nongshui);//写入eeprom
			Serial.println("制水时浓水设定成功");
			edp_upload_string("命令结果", "命令值有效，制水时浓水大小设定成功");
			
			f_zhuang_tai("命令值有效，制水时浓水大小设定成功");
			upload_alert_standard();//上报报警标准
		}
		EEPROM.commit();
		break;
		/*-------------------------------------------设定洗膜时浓水------------------------------------------*/

	case 3: //洗膜浓水 03{V}

		if ((buffer[1] == 45)) { value = int(buffer[46]) - 48; }
		if ((buffer[1] == 46)) {
			value = int(buffer[46] - 48) * 10; 	//Serial.println(value);
			value = value + (int(buffer[47]) - 48);
		}
		ximo_sudu = value;
		EEPROM.write(402, ximo_sudu);//写入eeprom
		Serial.println("洗膜时浓水设定成功");
		edp_upload_string("命令结果", "命令值有效，洗膜时浓水大小设定成功");
		upload_alert_standard();//上报报警标准
		
		f_zhuang_tai("命令值有效，洗膜时浓水大小设定成功");
		EEPROM.commit();
		break;
		/*-------------------------------------------压力桶失效时间设定------------------------------------------------*/
	case 4://04{V}
		if ((buffer[1] == 45)) { value = int(buffer[46]) - 48; }
		yalitong_shuiman_fenzhong = value;
		EEPROM.write(401, yalitong_shuiman_fenzhong);//写入eeprom

		Serial.println("压力桶失效时水满时间设定成功");
		edp_upload_string("命令结果", "命令值有效，压力桶失效时水满时间设定成功");
		edp_upload_int("压力桶失效检测时间", yalitong_shuiman_fenzhong);
		upload_alert_standard();//上报报警标准
		
		f_zhuang_tai("命令值有效，压力桶失效时水满时间设定成功");
		EEPROM.commit();
		break;
		/*-------------------------------------------程序更新-------------------------*/
	case 5://0599
		if ((buffer[1] == 46)) {
			value = int(buffer[46] - 48) * 10; 	//Serial.println(value);
			value = value + (int(buffer[47]) - 48);
		}
		if (value == 99)
		
		{
			chxu_update = true; 
			Serial.println("收到程序更新命令，计划在停机状态下更新");
			edp_upload_string("命令结果", "收到程序更新命令，计划在停机状态下更新");
		}
		
		/*-------------------------------------------缺省处理------------------------------------------*/

	default:
		//edp_upload_string("命令结果", "命令类型无效");
		break;
	}
	


}
void edp_first_connect()
{
	if (!edp_client.connected()) {
		edp_client.connect(edp_server, edp_port);
		Serial.print("连接到edp服务器：");
		Serial.println((String)edp_server);
	}
	if (edp_client.connected())
	{
		Serial.println("edp服务器连接成功");
		if (apikey != ""&& device_id != "")
		{
			edp.PacketConnect1(device_id.c_str(), apikey.c_str());
			Serial.println("生成了第一次edp包，准备发送");
			edp_packet_send();
			//Serial.println("yi jing fa song");
			//DeleteBuffer(&edp);
			edp.ClearParameter();

			Serial.println("删除了第一次连接包");
			edp_reponse(1);
			//rcvDebug(edp->_data, edp->_write_pos);//串口打印出edp包
		}
		else { Serial.println("设备ID和密码为空"); }
	}
	else
	{
		Serial.println("edp服务器连接失败");
	}
}

void   edp_heartbeat()
{
	//ESP.wdtFeed();
	if (edpConnected == true) {

		edp.PacketPing();//打包 192 0  0xC0 0

		rcvDebug(edp.GetData(), edp.GetWritepos());

		edp_packet_send();

		Serial.println("心跳包发出一次");

		//删除缓冲
		edp.ClearParameter();


		edp_reponse(2);//解包


	}
	else {
		Serial.println("心跳包定时发出，发现网络没有连接");

	}
}
void edp_packet_send()
{  // 发送之前清空接收缓存写入的内容 和未读的缓存
   //client.flush();
	while (edp_client.available()) { edp_client.read(); }
	if (!edp_client.connected()) {
		edp_client.connect(edp_server, edp_port);
		Serial.print("连接到edp服务器： ");
		Serial.println((String)edp_server);
	}
	if (edp_client.connected())
	{
		//Serial.println("edp服务器连接成功");

		edp_client.write(edp.GetData(), edp.GetWritepos());

		//Serial.println("edp包发出一次");
		//发送一次数据，心跳起算时间更新为最新
		if (ji_qi_shi_yan == true)//调试状态
		{
			ht__wangluo_dingshi = millis() + 60000; //间隔1分钟发送心跳.和检查网络情况
		}
		else//正常运行状态
		{
			ht__wangluo_dingshi = millis() + 200000; //间隔5分钟发送心跳.检查网络情况
		}
		//client.flush();//丢弃写入的任何字节，同时包括接收字节。此功能必须禁止	
	}
	else
	{
		Serial.println("edp服务器连接失败");
	}



}

void  edp_reponse(int response_type)
{
	ESP.wdtFeed();

	unsigned long time = millis() + 4000;
	while (!edp_client.available() && millis() < time);
	int	byte_count = edp_client.available();
	//memset(buffer, 0, 200);
	if (byte_count > 0) //有数据
	{
		//buffer=   client.readBytesUntil('\r')
		for (int e = 0; e < byte_count; e++)
		{
			buffer[e] = edp_client.read();
		}
		rcvDebug(buffer, byte_count);

		switch (response_type)
		{//设定为连接响应

		case  1:

			//Serial.println("edp连接响应");
			//0x20 0x02 0x0 0x0

			if (buffer[0] == 0x20 && buffer[2] == 0x00 && buffer[3] == 0x00)
			{
				Serial.println("EDP 连接成功");
				edpConnected = true;
				edp_disconnect_count = 0;
				f_zhuang_tai("EDP 连接成功");
			}

			else
			{
				edp_upload_string("edp连接", "收到连接响应数据，但是数据不正确");//可能发送连接正确，接收出现错误

				edpConnected = false;
				edp_disconnect_count++;//失败连接次数加1
				f_zhuang_tai("edp 连接不成功，等待下次连接");

				//log_write("edp 连接不成功，等待下次连接");//日志记录
				Serial.println("有数据，但数据不对，EDP连接失败");
			}

			break;
			//心跳反馈2个字节 208 0 或 0xD0 0
		case 2:
			//Serial.println("edp 心跳响应");					

			if (buffer[0] == B11010000  && buffer[1] == B00000000)
			{
				edpConnected = true;
				edp_disconnect_count = 0;//失败次数置零
				edp_upload_string("心跳", "收到心跳反馈数据，数据正确");//
				Serial.println("EDP请求心跳成功");
				f_zhuang_tai("收到心跳反馈数据，数据正确");
			}
			else
			{
				//心跳数据不对，可能发送正确，但接收错误，可能还处于连接状态，所以发送报警信息
				edp_upload_string("心跳", "收到心跳数据，但数据不对");
				f_zhuang_tai("收到心跳反馈数据，但数据不正确");
				edpConnected = false;//发送了信息后置假
				edp_disconnect_count++;//失败连接次数加1
				Serial.println("EDP请求心跳返回有数据但数据错误");

			}
			break;
		}
	}

	else {
		Serial.println("没有接收到数据");
		edpConnected = false;
		edp_disconnect_count++;//edp失败连接次数加1
		f_zhuang_tai("没有接收到EDP服务器数据");
	}

}

void edp_upload_int(String data_stream_id, int datapoint)//上传WiFi信号强度是负值
{
	if (edpConnected) {

		//ESP.wdtFeed();
		//Serial.println("上传了一次整数");
		String input = "{\"" + data_stream_id + "\":" + datapoint + "}\"";

		cJSON *cj = cJSON_Parse(input.c_str());

		edp.PacketSavedataJson("", cj, 3, 0);
		edp_packet_send();
		cJSON_Delete(cj);//如果不删除，nodemcu会重启
		while (edp_client.available()) { edp_client.read(); }//不接收存储确认
		edp.ClearParameter();

	}

}
void edp_upload_string(String data_stream_id, String datapoint) {
	if (edpConnected) {

		//ESP.wdtFeed();
		//Serial.println("上传了一次字符串");

		String input = ",;"; input += data_stream_id + ","; input += datapoint;//正确格式


																			   //EdpPacket*	edp = NewBuffer();

		edp.PacketSavedataSimpleString(device_id.c_str(), input.c_str());
		edp_packet_send();
		while (edp_client.available()) { edp_client.read(); }//不接收存储确认

													 //DeleteBuffer(&edp);
		edp.ClearParameter();


	}
}

void f_zhuang_tai(String state) {
	if (zhuang_tai_index >= 10) { zhuang_tai_index = 0; }
	zhangtai[zhuang_tai_index] = state; zhuang_tai_index++;
}
//eeprom读出
void eeprom_read() {

	for (int i = 0; i < 28; i++)//300-330 apikey
	{
		apikey+=(char)EEPROM.read(300 + i);
		
	}
	Serial.print("api_key:");  Serial.println(apikey);

	for (int i = 0; i < 10; i++)//340-350 device_id，10个
	{ char c= (char)EEPROM.read(340 + i);
	if (c != 0)	{device_id += c;}	
	}
	Serial.print("device_id:");  Serial.println(device_id);

	for (int i = 0; i < 15; i++)//360-370 wifi_ssid，15个
	{  char c= (char)EEPROM.read(360+i);

	if (c != 0)	{wifi_ssid += c;}
	}
	Serial.print("wifi_ssid:");  Serial.println(wifi_ssid);

	for (int i = 0; i <15; i++)//380-395 wifi_pass，15个
	{ 
	char c= (char)EEPROM.read(380+i);
	if (c != 0) { wifi_pass += c; }

	}
	Serial.print("wifi_pass:");  Serial.println(wifi_pass);

	nongshui = EEPROM.read(400);
	Serial.print("浓水:");  Serial.println(nongshui);
	yalitong_shuiman_fenzhong = EEPROM.read(401);
	Serial.print("压力桶水满:");  Serial.println(yalitong_shuiman_fenzhong);
	ximo_sudu = EEPROM.read(402);
	Serial.print("第15次洗膜数值:");  Serial.println(ximo_sudu);
	if (EEPROM.read(403) == 1) {
		kaiguan = true;  Serial.println("净水器允许运行");
	
	}
	else
	{
		kaiguan = false; Serial.println("净水器不允许运行");
	}
}

/*void log_write(String log) {
	//Serial.println("进入了日志记录");
	if (SPIFFS.begin())
	{
		if (SPIFFS.exists("/log.txt")) {
			File f = SPIFFS.open("/log.txt", "r");

			josn_string = f.readStringUntil(']') + ']';
			f.close();//关闭文件
			JsonArray& arr = jsonBuffer.parseArray(josn_string);

			delay(10);
			if (arr.success()) {
				//arr.printTo(Serial);

				while (arr.size() > 35)//最多35条储存，反复删除
				{

					arr.remove(0);//最旧去掉

				}
				////log初始有一个。["0"]
				arr.add(log);//增加一条

							 //arr.printTo(Serial);


				f = SPIFFS.open("/log.txt", "r+");//读取一次，打开一次。写入一次，打开一次
				arr.printTo(f);//保存数据
				Serial.println("日志记录成功");
				arr.end(); f.close(); SPIFFS.end();

			}
			else
			{//日志解析不成功
				Serial.println("日志解析不成功");
				File	s = SPIFFS.open("/log.txt", "r+");//读取一次，打开一次。写入一次，打开一次
														  //String zero = "0";
				JsonArray&	arr = jsonBuffer.parseArray("0");
				arr.printTo(s);//保存数据
				Serial.println("日志记录初始化成功");
				arr.end(); s.close(); SPIFFS.end();
			}

		}
		else
		{
			Serial.println("log.txt 不存在"); SPIFFS.end();
		}
	}
	else
	{
		Serial.println("文件系统打开失败");
	}
}
*/
void rcvDebug(unsigned char *rcv, int len)
{
	int i;

	Serial.print("接收字节个数: ");
	Serial.println(len, DEC);
	for (i = 0; i < len; i++)
	{
		Serial.print(rcv[i], 10);
		Serial.print(" ");
	}
	Serial.println("");
}
void upload_alert_standard()//上报报警标准
{
	String info = "制水时浓水报警标准："; info += (String)nongshui; info += "||洗膜时第15次浓水报警标准："; info += (String)ximo_sudu;
	info += "||最快压力桶水满报警分钟数标准：";	info += (String)yalitong_shuiman_fenzhong;
	edp_upload_string("报警标准", info);


}
void wifi_set(String ssid, String password)
{

	if (ssid != "") //无密码WiFi，密码为空
	{
		//delay(10);
		// We start by connecting to a WiFi network
		Serial.println();
		Serial.print("连接中...");
		Serial.println(ssid);
		if (wifi_pass != "")//需要密码
		{
			WiFi.begin(ssid.c_str(), password.c_str());
		}
		else
		{
			WiFi.begin(ssid.c_str());//不需要密码网络
		}

		for (int i7 = 0; i7 < 15; i7++)//最长等待15秒
		{
			ESP.wdtFeed();//防止连接wifi时间过长重启
			if (WiFi.status() != WL_CONNECTED) {
				delay(800);
				Serial.print(".");

			}
			else
			{
				wifi_connected = true; wifi_ip = WiFi.localIP();

				Serial.println("");
				Serial.print("连接到：");
				Serial.println(wifi_ssid);
				Serial.print("IP address: ");
				Serial.println(WiFi.localIP());
				//log_write("wifi连接成功");
				i7 = 20;//为了连接上WiFi立即退出循环
			}

		}

	}
}
void setup()
{//优先设置高压开关接地控制端接地，对原电路板干扰最小

	pinMode(D5, OUTPUT);//控制高压开关接地 
	if (kaiguan) {
		digitalWrite(D5, LOW);//高压开关接地，允许净水器运行
	}
	else {
		digitalWrite(D5, HIGH);//高压开关接高，不允许净水器运行
	}
  
	 pinMode(D1, INPUT_PULLUP);//超滤冲洗信号输入
pinMode(D2, INPUT_PULLUP); //废水水流传感器输入
	
	
	
	pinMode(D6, INPUT);//高压开关非接地端，拉高减少对原板干扰
	pinMode(D7, INPUT_PULLUP); //缺水信号输入,此脚可以上拉，拉高减少对原板干扰
	pinMode(D8, INPUT);//超滤冲洗信号输入，此脚不能拉高

    pinMode(D9, INPUT);//制水泵信号输入
	

	ESP.wdtEnable(WDTO_8S);
	ESP.wdtFeed();//setup函数怕运行超时

	
	EEPROM.begin(410);
Serial.begin(115200);//先开串口，才能显示有关信息
	eeprom_read();

				  //Serial.println("boot mode:" + ESP.getBootMode());
				  //Serial.println("cpu MH:" + ESP.getCpuFreqMHz());
	Serial.println("启动原因:" + ESP.getResetReason());
	Serial.println("启动信息:" + ESP.getResetInfo());
	//Serial.print("sdk_version:"); Serial.println(ESP.getSdkVersion());
wifi_set(wifi_ssid, wifi_pass);


	apStart();//建立web 服务器
	server.on("/", ap_root);
	server.on("/set.html", ap_set);//????AP????
	server.on("/input.html", ap_handle_para);
	//server.on("/log.html", ap_handle_log);
	server.begin();//启动web 服务器

	
	

	//if (MDNS.begin("esp8266")) { Serial.println("MDNS responder started"); }


	if (WiFi.status() == WL_CONNECTED)
	{
		MDNS.begin(host);
	httpUpdater.setup(&updateServer);
	updateServer.begin();
	MDNS.addService("http", "tcp", 80);
	Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", host);


		former_wifi_ssid = wifi_ssid;//记录现在值
		former_wifi_pass = wifi_pass;//记录现在值
		wifi_disconnect_count = 0;
		edp_first_connect();//edp第一次连接
	}
	else
	{

		wifi_ip = IPAddress(0, 0, 0, 0);
		//log_write("wifi连接失败");
		wifi_disconnect_count++;
		Serial.println("wifi连接失败");
		f_zhuang_tai("wifi连接失败");
	}
	String input = "";

	//input= "bootmode:" + ESP.getBootMode();
	input += "reset_reason：" + ESP.getResetReason();
	//input += "||cpu mh:" + ESP.getCpuFreqMHz();
	input += "||rerset_information:";
	input += ESP.getResetInfo();//包含异常详细信息
	input.remove(input.indexOf("epc1"));//字符串从索引位去掉后面的
										//input += "||ResetInfoPtr";	input += ESP.getResetInfoPtr();
										//input += "||FreeSketchSpace:"; input += ESP.getFreeSketchSpace();
	String info = "制水时浓水报警标准："; info += (String)nongshui; info += "||洗膜时第15次浓水报警标准："; info += (String)ximo_sudu;
	info += "||最快压力桶水满报警分钟数标准：";	info += (String)yalitong_shuiman_fenzhong;
	if (edpConnected == true)
	{	edp_upload_string("芯片复位一次", input);
		upload_alert_standard();
		Serial.println(WiFi.RSSI());
		edp_upload_int("WiFi信号强度", WiFi.RSSI());//接收信号强度
		edp_upload_string("目前版本", (String)version);//上传版本

	}


	//log_write(input);//日志记录一次
	//log_write(info);


	if (ji_qi_shi_yan == true)//调试状态
	{
		Serial.println("调试状态");

		ht__wangluo_dingshi = millis() + 60000; //间隔1分钟发送心跳.和检查网络情况
	}
	else//正常运行状态
	{
		Serial.println("工作状态");

		ht__wangluo_dingshi = millis() + 120000; //间隔600秒10分钟发送心跳.检查网络情况
	}

	if (kaiguan == true)
	{
		edp_upload_int("开关", 1);
		kaiguan_dingshi = millis() + 300000;//5分钟
	}



	sensors.begin();//测机箱内温度
	if (sensors.getAddress(insideThermometer, 0))
	{
		Serial.println("得到温度器件地址");
		edp_upload_string("工作状态", "得到温度器件地址");
		//log_write("得到温度器件地址");
		f_zhuang_tai("得到温度器件地址");
		sensors.setResolution(insideThermometer, 9);
	}
}

void loop()
{
	server.handleClient();
	updateServer.handleClient();//




	/*------------------------------------定时上传温度------------------------------------------*/
	if (millis() > wendu_dingshi)
	{
		sensors.requestTemperatures(); // Send the command to get temperatures


		float tempC = sensors.getTempC(insideThermometer);
		Serial.println(tempC);
		//log_write("机箱内温度：" + (String)tempC + "");
		f_zhuang_tai("机箱内温度：" + (String)tempC + "");
		edp_upload_string("机箱内温度", (String)tempC);
		if (tempC < 2) { edp_upload_string("机箱内温度", "机箱内温度过低：" + (String)tempC + ""); }
		wendu_dingshi = millis() + 300000;

	}

	/*------------------------------------定时发出心跳和网络检查------------------------------------------*/
	//heart_beat_netcheck();//心跳发送和网络检查
	if (millis() > ht__wangluo_dingshi)
	{
		edp_heartbeat();

		if (edpConnected == false && state == "ting_ji")//网络连接状态，edp连接状态就不进行网络检查了
		{
			edp_check_net();
		}

	}

	/*------------------------------------定时发出是否允许净水器运行------------------------------------------------*/
	if (millis() > kaiguan_dingshi)
	{
		if (kaiguan == true)
		{
			digitalWrite(D5, LOW);//高压开关接地端接地，正常工作	
			edp_upload_int("开关", 1);
		}
		else
		{
			digitalWrite(D5, HIGH);//高压开关接地端接高电位，停止工作	
			edp_upload_int("开关", 0);
		}

		kaiguan_dingshi = millis() + 300000;//5分钟

	}
	/*------------------------------------接收到命令处理------------------------------------------------*/
	while (edp_client.available())
	{	//	readEdpPkt(&rcv_pkt);
		int	byte_count = edp_client.available();
		if (byte_count > 0) //有数据
		{

			for (int e = 0; e < byte_count; e++)
			{
				buffer[e] = edp_client.read();
			}
			rcvDebug(buffer, byte_count);
			edp_command_parse();
			//memset(buffer, 0, 200);
		}
	}

	/*-----------------------------超滤冲洗时水量过少时处理-------------------*/
	if (chlv_chxi_gu_zhang >= 5)
	{
		f_zhuang_tai("超滤冲洗时水量过少，可能电磁阀不能打开和超滤堵塞，联系售后");

		edp_upload_string("报警", "超滤冲洗时水量过少，可能电磁阀不能打开和超滤堵塞，联系售后");
		edp_upload_string("工作状态", "超滤冲洗时水量过少，可能电磁阀不能打开和超滤堵塞，联系售后");
		//log_write("超滤冲洗时水量过少，可能电磁阀不能打开和超滤堵塞，联系售后");


		chlv_chxi_gu_zhang = 0;//只进入一次

	}
	/*-洗膜后的停机状态检测超滤，有水量，证明电磁阀漏水，电磁阀漏水会造成洗膜脓水检测失效-------------------*/

	if (chlv_tingji_gu_zhang >= 40)//此值设大为了避免制满水后很快重启，误认为洗膜水为超滤泄露水。
	{
		f_zhuang_tai("停机状态检测超滤漏水，联系售后");

		edp_upload_string("报警", "停机状态检测超滤漏水，联系售后");
		edp_upload_string("工作状态", "停机状态检测超滤漏水，联系售后");
		//log_write("停机状态检测超滤漏水，联系售后");
		return;

	}

	/*------------------------------------洗膜时废水过少时处理------------------------------------------------*/
	if (ximo_guzhang >= 2) {


		if (state != "xi_mo_alarm")//目的是进入一次
		{
			f_zhuang_tai("净水洗膜时浓水过少，可能气囊破失效和膜穿保护停机中，联系售后");

			edp_upload_string("报警", "净水洗膜时浓水过少，可能气囊破失效和膜穿保护停机中，联系售后");
			edp_upload_string("工作状态", "净水洗膜时浓水过少，可能气囊破失效和膜穿保护停机中，联系售后");
			//log_write("净水洗膜时浓水过少，可能气囊破失效和膜穿保护停机中，联系售后");

			digitalWrite(D5, HIGH);
		}
		state = "xi_mo_alarm";
		ESP.wdtFeed();//重复返回会让看门狗重启芯片，必须喂狗
		return;
	}

	/*------------------------------------制水时废水过少时处理------------------------------------------------*/
	if (zhishui_xiao_gu_zhang >= 5)
	{
		if (state != "zhi_shui_alarm") //运行一次
		{
			if (ji_qi_shi_yan == false) { time_local = millis() + 1800000; }//为半小时停机
			else {
				time_local = millis() + 300000;//实验状态5分钟
			}
			digitalWrite(D5, HIGH);//让高压开关接地端接高位3.3v
			f_zhuang_tai("制水时浓水太少，联系售后，半小时后重启");
			//log_write("制水时浓水太少，联系售后，半小时后重启");
			Serial.println("废水过少，故障保护停机");
			edp_upload_string("报警", "制水时浓水过少，联系售后，半小时后重启");
			edp_upload_string("工作状态", "制水时浓水过少，联系售后，半小时后重启");

		}
		state = "zhi_shui_alarm";
		if (millis() < time_local)//停机半小时时间内
		{
			ESP.wdtFeed();//喂狗
			return;
		}
		else
		{
			digitalWrite(D5, LOW); //设置高压开关接地
			zhishui_xiao_gu_zhang = 0;//为了下次循环离开故障处理

			f_zhuang_tai("重启中");
			//log_write("重启中");
			edp_upload_string("工作状态", "制水时废水过少保护停机，过半小时后的重启中");
			edp_upload_string("报警", "制水时废水过少保护停机，过半小时后的重启中");
		}
	}

	/*------------------------------------缺水时或不缺水时，和其他状态是共存关系------------------------------------------------*/
	if (digitalRead(D7) == HIGH)//不管净水器什么状态，缺水和水满状态互锁，只发送一次数据
	{
		if (queshui_1_1 == false)
		{
			Serial.println("净水器缺水状态");
			//log_write("工作状态:净水器缺水状态");
			edp_upload_string("工作状态", "净水器缺水状态");
			edp_upload_string("报警", "净水器缺水状态");
			edp_upload_int("超滤是否缺水", 0);
			f_zhuang_tai("净水器缺水状态");

			queshui_1_1 = true;//缺水时报警一次
			queshui_1_2 = false;//为正常供水时运行一次复位
		}
	}

	else
	{
		if (queshui_1_2 == false)
		{
			edp_upload_int("超滤是否缺水", 1);
			queshui_1_2 = true;//为了运行一次
			queshui_1_1 = false;//为缺水时报警复位 
		}
	}

	/*------------------------------------超滤冲洗等待或冲洗间隔阶段------------------------------------------------*/
	if (digitalRead(D9) == LOW && digitalRead(D6) == LOW && digitalRead(D8) != HIGH)//
	{


		if (state != "chao_lv_xi_mo")//目的是运行一次
		{
			Serial.println("净水器超滤冲洗等待或保护状态");

			//log_write("工作状态:净水器超滤冲洗等待或保护状态");
			edp_upload_string("工作状态", "净水器超滤冲洗等待或保护状态");
			f_zhuang_tai("净水器超滤冲洗等待或保护状态");

		}
		state = "chao_lv_xi_mo";
	}


	/*------------------------------------冲洗超滤时处理------------------------------------------------*/
	//20A流量传感器接在废水管路上，可以检测超滤冲洗时的水量大小，
	//可以检测电磁阀不能打开和超滤堵塞
	if (digitalRead(D8) == HIGH)
	{
		if (state != "chao_lv_chxi")
		{
			Serial.println("净水器正在冲洗超滤");
			//log_write("工作状态:净水器正在冲洗超滤");
			edp_upload_string("工作状态", "净水器正在冲洗超滤");
			f_zhuang_tai("净水器正在冲洗超滤");
			time_local = millis() + 200;//初次加上1秒，为了延时
			i2 = 0;//数组索引从0开始

			state = "chao_lv_chxi";
		}
		//
		if (millis() > time_local)
		{
			chlv_detect(500);	//1秒

			if (i2 >= 15) { i2 = 0; }
			chaolv_chxi[i2] = chlv_count; i2++;
			//ESP.wdtFeed();
			edp_upload_int("超滤冲洗水量", chlv_count);
			Serial.print("超滤冲洗:"); Serial.println(chlv_count);
			if (chlv_count < chaolv_chxi_biaozhun) { chlv_chxi_gu_zhang++; }
			else { chlv_chxi_gu_zhang = 0; }

			time_local = millis() + 200;//加上500毫秒，下次循环进入做准备
		}

	}

	/*------------------------------------制水时处理------------------------------------------------*/
	if (digitalRead(D9) == HIGH && digitalRead(D6) == LOW)//制水状态，高压开关没有断开
	{
		unsigned long jingshui_dcf;//制水超过一个小时后，净水电磁阀得电，关闭净水输出

		if (state != "zhi_shui")	//只运行一次 
		{
			begintime_zhishui = millis();//制水开始时间复位
			//digitalWrite(D1, LOW);//关闭废水电磁阀，防止高压开关反复开闭，造成废水电磁阀不能关闭
			f_zhuang_tai("制水中");

			Serial1.println("制水启动");

			edp_upload_string("工作状态", "制水启动");

			//log_write("工作状态，制水启动");
			time_local = millis() + 10000;//初次加上10秒，为了延时
			i2 = 0;//数组索引从0开始
			zhishui_1_1 = false; zhishui_1_2 = false;//为两个运行一次复位
			state = "zhi_shui";
		}
		//为了10秒钟检测一次
		if (millis() > time_local)
		{
			detect(1000);	//1秒

			if (i2 >= 10) { i2 = 0; }
			zhishui[i2] = count; i2++;
			ESP.wdtFeed();
			edp_upload_int("制水时浓水", count);
			Serial.print("制水:"); Serial.println(count);
			if (count < nongshui) { zhishui_xiao_gu_zhang++; }
			else { zhishui_xiao_gu_zhang = 0; }
			time_local = millis() + 10000;//加上10秒下次循环进入做准备			
		}
	}

	/*------------------------------------制水快停机转换到洗膜状态------------------------------------------------*/
	if (digitalRead(D9) == HIGH && digitalRead(D6) == HIGH)//维持3秒左右
	{
		if (state != "zhi_shui>ximo")//目的是运行一次
		{
			Serial.println("净水器制水结束到洗膜转换");

			//log_write("工作状态:净水器制水结束到洗膜转换");

			edp_upload_string("工作状态", "净水器制水结束到洗膜转换");
			f_zhuang_tai("净水器制水结束到洗膜转换");
			xu_yao_ximo = true;

			//digitalWrite(D1, HIGH);//打开废水电磁阀
		}

		state = "zhi_shui>ximo";
	}

	/*------------------------------------洗膜状态处理------------------------------------------------*/
	if (digitalRead(D9) == LOW&&digitalRead(D6) == HIGH) //净水洗膜状态，高压开关断开，泵停止工作
	{
		if (xu_yao_ximo == true)
		{
			if (state != "ximo")//需要洗膜为真时，洗膜状态。制水状态置真
			{
				digitalWrite(D9, LOW);//关闭废水电磁阀
				endtime_zhishui = millis();//制水结束时间赋值
				time_local1 = millis() + 300000;//洗膜持续5分钟
				time_local2 = millis() + 3000;//初次加3秒，不避让水满后的超滤冲洗
		/*--------------------------压力桶报警-----------------------------------*/
				if ((endtime_zhishui - begintime_zhishui) < yalitong_shuiman_fenzhong * 60000)//判断压力桶漏气 破 高压开关动作故障 膜穿，默认5分钟
				{
					//ximo_baojing = false;
					edp_upload_string("压力桶 膜穿 高压开关", "制水开始至水满停机时间过短，疑似压力桶漏气或进水关闭，气囊破，膜穿，高压开关动作不可靠");
				}


				if ((endtime_zhishui - begintime_zhishui) > 2000)//制水开始到停机计算一次运行时间累计,255分钟进制
				{
					unsigned char flag1 = EEPROM.read(256);//索引从0开始,256-1，放的数据是255的倍数
					unsigned char flag2 = EEPROM.read(257);//256位置满255进位后的倍数放在257位置

					minute = (endtime_zhishui - begintime_zhishui) / 60000;//变成分钟
					edp_upload_int("单次制水时间:分钟", minute);//云端上报单次制水时长。
					//log_write("单次制水时间:分钟" + (String)minute);

					unsigned int raw_minute = EEPROM.read(flag1);
					unsigned int total = minute + raw_minute;

					if (total >= 255)//进位
					{
						EEPROM.write(256, flag1 + 1); EEPROM.write(flag1 + 1, (total - 255));
						unsigned int total_minute = (flag2 * 65025) + ((flag1 + 1) * 255);//总分钟数
						worktime_h = itoa(total_minute / 60, string, 10);//小时
						worktime_m = itoa(total - 255, string, 10); //剩余分钟

					}
					else
					{
						unsigned int	total_minute = (flag2 * 65025) + (flag1 * 255);//总分钟数
						worktime_h = itoa(total_minute / 60, string, 10);//小时
						worktime_m = itoa(total, string, 10); //分钟
						EEPROM.write(flag1, total);
					}

					if (flag1 >= 255)
					{
						EEPROM.write(256, 0); //进位后为0
						EEPROM.write(257, flag2++);//放进位后的数字
					}

					EEPROM.commit();
					String input = worktime_h; input += " 小时 "; input += worktime_m; input += " 分钟 ";
					edp_upload_string("总运行时间", input);
					//edp_upload_string("shj", "test");
					//log_write("总运行时间:" + input);

				}
				Serial.println("洗膜开始");
				edp_upload_string("工作状态", "制水结束，洗膜开始");
				f_zhuang_tai("制水结束，洗膜开始");

				//log_write("工作状态 : 制水结束,洗膜开始");
				i2 = 0;
				state = "ximo";
			}

			if (millis() < time_local1)//在检测时长5分钟内，
			{
				if (millis() > time_local2)//超过5秒
				{

					detect(1000);//1秒				
					if (i2 >= 40) { i2 = 0; }
					ximo[i2] = count; i2++;
					edp_upload_int("洗膜时浓水", count);
					ximo_cishu++;//水流测速一次加一次
					if (endtime_zhishui - begintime_zhishui > 240000)//防止制水时间过短造成洗膜时检测浓水流量造成假报警
					{
						if (ximo_cishu == 15)//第15次判断洗膜大小
						{
							if (count < ximo_sudu) { ximo_guzhang++; }
						}
					}
					Serial.print("洗膜:");
					Serial.print(ximo_cishu);
					Serial.print(":");
					Serial.println(count);
					time_local2 = millis() + 5000;//加上5秒
				}
			}
			else//超过了洗膜设定时间5分钟
			{
				Serial.println("工作状态:洗膜结束");
				edp_upload_string("工作状态", "洗膜结束");
				f_zhuang_tai("洗膜结束");
				xu_yao_ximo = false;
				digitalWrite(D1, LOW);//让电磁阀失电，恢复净水输出
				//xi_guo_mo = true;
			}

		}
	}

	/*------------------------------------停机状态处理------------------------------------------------*/
	if (digitalRead(D9) == LOW&&digitalRead(D6) == HIGH)//洗过膜,高压开关断开了，停机状态。
	{
		if (xu_yao_ximo == false) //停机状态
		{


			if (state != "ting_ji")

			{
				Serial.println("净水器水满停机状态");
				f_zhuang_tai("净水器水满停机状态");
				//log_write("工作状态:水满停机中");
				edp_upload_string("工作状态", "水满停机中");
				jiancha_chaolv = millis() + 60000;//一分钟后检查超滤是否漏水

				state = "ting_ji";
			}


			if (millis() > jiancha_chaolv&&digitalRead(D8) != HIGH&&digitalRead(D9) != HIGH
				&&digitalRead(D6) == HIGH)
				//增压泵不启动，超滤不冲洗，水满状态
			{
				detect(1000);	//1秒
				if (count > 5)
				{
					edp_upload_string("超滤是否漏水", "超滤电磁阀漏水");
					chlv_tingji_gu_zhang++;
					f_zhuang_tai("超滤电磁阀漏水");
				}
				else
				{
					edp_upload_string("超滤是否漏水", "超滤电磁阀不漏水");
					f_zhuang_tai("超滤电磁阀不漏水");
					chlv_tingji_gu_zhang = 0;
				}

				jiancha_chaolv = millis() + 300000;//5分钟后检查超滤电磁阀是否漏水
            }

		 if (chxu_update == true && digitalRead(D8) != HIGH&&digitalRead(D9) != HIGH
			 &&digitalRead(D6) == HIGH)
			{   	
				//digitalWrite(D5, HIGH);//高压开关接高，不允许净水器运行
			
			 if (!update_client.connected()) {
				 update_client.connect("lyd50.vicp.io",81);//更新协议是网址，不带协议前缀
				 Serial.print("连接到程序更新服务器：");
				 Serial.println(update_client.remoteIP()); 
				 String   s = "更新IP：";
				 s += (String)update_client.remoteIP();
				 edp_upload_string("命令结果", s);
			 }
			 if (update_client.connected())
			 {		//Serial.println("1234");
					t_httpUpdate_return ret = ESPhttpUpdate.update("http://lyd50.vicp.io:81/20a.bin");
					switch (ret) {
					case HTTP_UPDATE_FAILED:
						Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
						edp_upload_string("命令结果", "程序更新失败");
						break;

					case HTTP_UPDATE_NO_UPDATES:
						Serial.println("HTTP_UPDATE_NO_UPDATES");
						edp_upload_string("命令结果", "没有执行程序更新");
						break;

					case HTTP_UPDATE_OK:
						Serial.println("HTTP_UPDATE_OK");
						edp_upload_string("命令结果", "程序更新成功");
						//chxu_update = false;
						break;
					}
				}
                 }
			
		 
		 if (wifi_disconnect_count == 3 || edp_disconnect_count == 3)//两个连接不成功次数超过2次，停机状态下重起
				{
					Serial.print("wifi连接不成功次数："); Serial.println(wifi_disconnect_count);
					Serial.print("edp连接不成功次数："); Serial.println(edp_disconnect_count);

					ESP.reset();
				}

			


		}


		//ESP.wdtFeed();//空循环会重启。

	}
}
