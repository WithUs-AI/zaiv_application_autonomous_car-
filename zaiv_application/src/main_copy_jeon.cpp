
#include <iostream>

#include <algorithm>
#include <future>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <cstdint>
#include <iomanip>
#include <array>
#include <queue>
#include <time.h>
#include <memory>
#include <unordered_set>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <opencv2/opencv.hpp>

#include "hailo/hailort.hpp"
#include "hailos/hailo_objects.hpp"
#include "hailos/hailo_tensors.hpp"
#include "hailos/double_buffer.hpp"
#include "hailos/hailo_common.hpp"
#include "postprocess/yolo/yolo_postprocess.hpp"

#include "interfaces/websocket/server_ws.hpp"

#include "rapidjson/document.h"

#include "configuration.h"

/////////////////////////////////////////////////////////////////////////////
//                                 COMMON                                  //
/////////////////////////////////////////////////////////////////////////////
using namespace std;
using namespace rapidjson;

#define CONFIGURATION_FN "./zaiv_application.cfg"

/////////////////////////////////////////////////////////////////////////////
//                                 SERIAL                                  //
/////////////////////////////////////////////////////////////////////////////
int uart0_filestream = -1;

/////////////////////////////////////////////////////////////////////////////
//                              HAILO DEFINES                              //
/////////////////////////////////////////////////////////////////////////////

//#define DEFAULT_YOLO_HEF_FILE ("../hefs/coco_nonms_eth_nonms_eth_hailo0913.hef")
#define DEFAULT_HEF_FILE ("../hefs/coco_nonms_eth_nonms_eth_hailo0913.hef")
//#define DEFAULT_MOBILENET_HEF_FILE ("./ssd_Mobilenet.hef")




#define ______USE_NMS_HEF______
#ifdef ______USE_NMS_HEF______
#undef DEFAULT_HEF_FILE
#define DEFAULT_HEF_FILE ("../hefs/track_kss.hef")
#endif





#define MAX_BOXES 50

#define REQUIRE_ACTION(cond, action, label, ...)                \
	do {                                                        \
		if (!(cond)) {                                          \
			std::cout << (__VA_ARGS__) << std::endl;            \
			action;                                             \
			goto label;                                         \
		}                                                       \
	} while(0)

#define REQUIRE_SUCCESS(status, label, ...) REQUIRE_ACTION((HAILO_SUCCESS == (status)), , label, __VA_ARGS__)

#define NO_GLOBAL_ID_COLOR (cv::Scalar(255, 0, 0))
#define GLOBAL_ID_COLOR (cv::Scalar(0, 255, 0))

#define SPACE " "

#define NULL_COLOR_ID ((size_t)NULL_CLASS_ID)
#define DEFAULT_COLOR (cv::Scalar(255, 255, 255))

#define YMIN 0
#define XMIN 1
#define YMAX 2
#define XMAX 3

#define MAX_CLASSES 5

/////////////////////////////////////////////////////////////////////////////
//                             HAILO VARIABLES                             //
/////////////////////////////////////////////////////////////////////////////

std::array<std::queue<std::vector<uint8_t>>, INFERNCE_MAX_OUTPUT_COUNT> _featuresbuffer;

bool _terminate = false;
bool _thread_terminate = false;
bool _thread1_terminated = false;
bool _thread2_terminated = false;
bool _thread3_terminated = false;

std::mutex _m1, _m2, _m3;

int _fps = 0;


std::queue<cv::Mat> _frame_queue;
std::queue<HailoROIPtr> _frame_detections;

/////////////////////////////////////////////////////////////////////////////
//                                 COMMON                                  //
/////////////////////////////////////////////////////////////////////////////
ZaivCfg *_pConfig_obj = NULL;

/////////////////////////////////////////////////////////////////////////////
//                            WS SERVER DEFINES                            //
/////////////////////////////////////////////////////////////////////////////
#define WS_SERVER_PORT 8080

/////////////////////////////////////////////////////////////////////////////
//                           WS SERVER VARIABLES                           //
/////////////////////////////////////////////////////////////////////////////
using WsServer = SimpleWeb::SocketServer<SimpleWeb::WS>;
WsServer* _pServer = NULL;

/////////////////////////////////////////////////////////////////////////////
//                                FUNCTIONS                                //
/////////////////////////////////////////////////////////////////////////////

YoloParams* _init_params;

// int _car_move_value[2][2] = {{70, 70}, {70, 255}};//[straight/turn][left/right]
// int _car_ref_center = 400;
int _car_diff_value[2] = {0, 0};

int bottomRectY1 = 0;
int bottomRectY2 = 0;
int bottomRectX1 = 0;
int bottomRectX2 = 0;
int Direction = 0;

#define NO_GLOBAL_ID_COLOR (cv::Scalar(255, 0, 0))
#define GLOBAL_ID_COLOR (cv::Scalar(0, 255, 0))
#define SPACE " "

std::string confidence_to_string(float confidence)
{
	int confidence_percentage = (confidence * 100);

	return std::to_string(confidence_percentage) + "%";
}

static std::string get_detection_text(HailoDetectionPtr detection, bool show_confidence = true)
{
	std::string text;
	std::string label = detection->get_label();
	std::string confidence = confidence_to_string(detection->get_confidence());
	if (!show_confidence)
	text = label;
	else if (!label.empty())
	{
		text = label + SPACE + confidence;
	}
	else
	{
		text = confidence;
	}
	return text;
}

#define NULL_COLOR_ID ((size_t)NULL_CLASS_ID)
#define DEFAULT_COLOR (cv::Scalar(255, 255, 255))

static const std::vector<cv::Scalar> color_table = {
	cv::Scalar(255, 0, 0), cv::Scalar(0, 255, 0), cv::Scalar(0, 0, 255), cv::Scalar(255, 255, 0), cv::Scalar(0, 255, 255),
	cv::Scalar(255, 0, 255), cv::Scalar(255, 170, 0), cv::Scalar(255, 0, 170), cv::Scalar(0, 255, 170), cv::Scalar(170, 255, 0),
	cv::Scalar(170, 0, 255), cv::Scalar(0, 170, 255), cv::Scalar(255, 85, 0), cv::Scalar(85, 255, 0), cv::Scalar(0, 255, 85),
	cv::Scalar(0, 85, 255), cv::Scalar(85, 0, 255), cv::Scalar(255, 0, 85), cv::Scalar(255, 255, 255) };

cv::Scalar indexToColor(size_t index)
{
	return color_table[index % color_table.size()];
}

static cv::Scalar get_color(size_t color_id)
{
	cv::Scalar color;
	if (NULL_COLOR_ID == color_id)
	color = DEFAULT_COLOR;
	else
	color = indexToColor(color_id);

	return color;
}

#define TEXT_FONT_FACTOR (0.12f)

bool show_confidence = true;
void connect_Serial()
{
	uart0_filestream = open(_pConfig_obj->m_extentions.car.device_path.c_str(), O_RDWR | O_NOCTTY | O_NDELAY); //Open in non blocking
	if(uart0_filestream == -1)
	{
		cout << "Serial Connect" << "Error: " << uart0_filestream << " Can Not Open "<< _pConfig_obj->m_extentions.car.device_path.c_str() << endl;
	}

	struct termios options;

	tcgetattr(uart0_filestream,&options);
	options.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
	options.c_iflag = IGNPAR;
	options.c_oflag = 0;
	options.c_lflag = 0;
	tcflush(uart0_filestream,TCIFLUSH);
	tcsetattr(uart0_filestream, TCSANOW, &options);
}

std::string read_command()
{
    std::string ret = "";
    if (uart0_filestream != -1)
    {
        // Read up to 255 characters from the port if they are there
        unsigned char rx_buffer[256];
        int rx_length = read(uart0_filestream, (void *)rx_buffer, 255); // Filestream, buffer to store in, number of bytes to read (max)
        if (rx_length < 0)
        {
            // An error occured (will occur if there are no bytes)
        }
        else if (rx_length == 0)
        {
            // No data waiting
        }
        else
        {
            // Bytes received
            rx_buffer[rx_length] = '\0';
            //printf("%i bytes read : %s\n", rx_length, rx_buffer);
            ret.append((const char*)rx_buffer);
        }
    }

    return ret;
}

void send_command(std::string cmd,int motorR, int motorL)
{
    // if (uart0_filestream == -1)
    // {
    //     connect_Serial();
    // }

    //     cout << cmd << endl;
	if(cmd.compare("move")==0)
	{
		char tx_buffer[50]={0,};
        char checksum_text[10];
		char *p_tx_buffer;
        unsigned char checksum=0;
		sprintf(tx_buffer,"move:%d:%d:",motorR,motorL);
		cout << tx_buffer << endl;

		// if(uart0_filestream==-1) cout << "connect Fail" <<endl;
		// else
		{
			//cout << (sizeof(tx_buffer)) << endl;
            for(int i=0;i<strlen(tx_buffer);i++)
            {
                checksum += tx_buffer[i];
            }
            sprintf(checksum_text,"%d\r\n",checksum);
            strcat(tx_buffer,checksum_text);

			int count = write(uart0_filestream,&tx_buffer[0],(strlen(tx_buffer)));

			//cout << "length: " << (strTest.length()) << endl;
			//cout << "size: " << (strTest.size()) << endl;
			//int count = write(uart0_filestream,strTest.c_str(),strTest.length());

			if(count<0)
			{
				cout << "Serial Send" << "Error: " << " Can Not Send Data TX error" << endl;
			}
		}
	}
	else
	{
		char tx_buffer[5];
		int count = write(uart0_filestream,&tx_buffer[0],(sizeof(tx_buffer)));
	}
}
/*
void send_command(std::string cmd,int motorR, int motorL)
{
	cout << cmd << endl;
	if(cmd.compare("move")==0)
	{
		char tx_buffer[20]={0,};
		char *p_tx_buffer;

		sprintf(tx_buffer,"move:%d:%d\r\n",motorR,motorL);
		cout << tx_buffer << endl;

		if(uart0_filestream==-1) cout << "connect Fail" <<endl;
		else
		{
			//cout << (sizeof(tx_buffer)) << endl;

			int count = write(uart0_filestream,&tx_buffer[0],(strlen(tx_buffer)));
			//cout << "length: " << (strTest.length()) << endl;
			//cout << "size: " << (strTest.size()) << endl;
			//int count = write(uart0_filestream,strTest.c_str(),strTest.length());

			if(count<0)
			{
				cout << "Serial Send" << "Error: " << " Can Not Send Data TX error" << endl;
			}
		}
	}
	else
	{
		char tx_buffer[5];
		int count = write(uart0_filestream,&tx_buffer[0],(sizeof(tx_buffer)));
	}
}
*/

bool checkIntersection(const cv::Rect& rect1, const cv::Rect& rect2) {
	if (rect1.x < rect2.x + rect2.width &&
		rect1.x + rect1.width > rect2.x &&
		rect1.y < rect2.y + rect2.height &&
		rect1.y + rect1.height > rect2.y) {
		return true;
	}
	return false;
}

bool comparePoints(const cv::Point& pt1, const cv::Point& pt2)
{
    return pt1.x < pt2.x;
}

void draw_all_send_data(cv::Mat *frame, HailoROIPtr roi, std::list<Classes*> &classes)
{
	rapidjson::StringBuffer ss;
	rapidjson::Writer<rapidjson::StringBuffer> writer(ss); // Can also use Writer for condensed formatting

	std::string module = "zaiv";
	std::string method = "push";
	std::string cmd = "subscribe";
	std::string attr = "infer-metadata";

	writer.StartObject();
	writer.String("module");
	writer.String(module.c_str(), static_cast<rapidjson::SizeType>(module.length()));
	writer.String("method");
	writer.String(method.c_str(), static_cast<rapidjson::SizeType>(method.length()));
	writer.String("cmd");
	writer.String(cmd.c_str(), static_cast<rapidjson::SizeType>(cmd.length()));
	writer.String("attr");
	writer.String(attr.c_str(), static_cast<rapidjson::SizeType>(attr.length()));
	writer.String("payload");
	writer.StartArray();

	cv::Rect redcone = cv::Rect();
	cv::Rect greencone = cv::Rect();



	/////////draw_map
	int rectWidth = 370;
	int rectHeight = 30;

	int startY = frame->size().height - rectHeight;
    int endY = 85;
	int rectStep = 25;
	
	std::vector<cv::Rect> areas;
	std::vector<cv::Point> centerPoint1; //class 1의 좌표
	std::vector<cv::Point> centerPoint2; //class 1의 좌표
	
	/*while (startY >= endY && rectWidth > rectStep) {
		cv::Rect rect((frame->size().width - rectWidth) / 2, startY, rectWidth, rectHeight);
		areas.push_back(rect);
		cv::rectangle(*frame, rect, cv::Scalar(0, 255, 0), 1);
		rectWidth -= rectStep;
		startY -= rectHeight;
	}

	//std::reverse(areas.begin(), areas.end());

	cv::Point startPoint(frame->size().width / 2, 0);
	cv::Point endPoint(frame->size().width / 2, frame->size().height);

	cv::line(*frame, startPoint, endPoint, cv::Scalar(51, 255, 255), 1);*/
	
	bottomRectY1 = 0;
	bottomRectY2 = 0;
	bottomRectX1 = 0;
	bottomRectX2 = 0;
	Direction = 0;

	cv::Point Rect1 = cv::Point(0, 0);
	cv::Point Rect2 = cv::Point(512, 0);
	
	for (HailoObjectPtr obj : roi->get_objects())
	{
		if (obj->get_type() == HAILO_DETECTION)
		{
			HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);
			std::string label = detection->get_label();

			bool matched = false;
			Classes *classesmatched;
			for(auto &each : classes)
			{
				if(label == each->name)
				{
					classesmatched = each;
					matched = true;
					break;
				}
			}

			if(!matched) continue;
			if(!classesmatched->show) continue;
			if(detection->get_confidence() < _pConfig_obj->m_Ai.inference.confidence) continue;

            size_t idx = (size_t)detection->get_class_id();
            if (idx >= _pConfig_obj->m_Ai.model.classes.size()) continue;

            auto iter = _pConfig_obj->m_Ai.model.classes.begin();
            std::advance(iter, idx);
            if ((*iter)->show == false) continue;

            cv::Scalar color = NO_GLOBAL_ID_COLOR;
            std::string text = "";

			color = get_color((size_t)detection->get_class_id());
			text = get_detection_text(detection, show_confidence);

			HailoBBox roi_bbox = hailo_common::create_flattened_bbox(roi->get_bbox(), roi->get_scaling_bbox());
			auto detection_bbox = detection->get_bbox();

			auto bbox_min = cv::Point(
			((detection_bbox.xmin() * roi_bbox.width()) + roi_bbox.xmin()) * frame->size().width,
			((detection_bbox.ymin() * roi_bbox.height()) + roi_bbox.ymin()) * frame->size().height);
			auto bbox_max = cv::Point(
			((detection_bbox.xmax() * roi_bbox.width()) + roi_bbox.xmin()) * frame->size().width,
			((detection_bbox.ymax() * roi_bbox.height()) + roi_bbox.ymin()) * frame->size().height);



			cv::Rect rect = cv::Rect(bbox_min, bbox_max);
			
			cv::Point center(rect.x + rect.width / 2, rect.y + rect.height / 2);
			if((size_t)detection->get_class_id() == 1) 
			{
				centerPoint1.push_back(center);
			}
			else if((size_t)detection->get_class_id() == 2) 
			{
				centerPoint2.push_back(center);
			}
			
			circle(*frame, center, 3, cv::Scalar(0, 255, 0), -1);
			
			int rectX = rect.x + (rect.width / 2);
			int rectY = rect.y + rect.height;
			
			if(rectY < 350)
			{
				if((size_t)detection->get_class_id() == 2)
				{
					if (bottomRectY2 < rectY)
					{
						bottomRectY2 = rectY;
						bottomRectX2 = rectX;
						Rect2 = center;	
					}
				}
				else if((size_t)detection->get_class_id() == 1)
				{
					if (bottomRectY1 < rectY)
					{
						bottomRectY1 = rectY;
						bottomRectX1 = rectX;
						Rect1 = center;
					}
				}
			}

			cv::rectangle(*frame, rect, color);
			
			
			//cv::Point((bbox_min.x + bbox_max.x)/2,(bbox_min.y + bbox_max.y)/2);
			// Draw text
			cv::Point text_position = cv::Point(rect.x - log(rect.width), rect.y - log(rect.width));
			float font_scale = TEXT_FONT_FACTOR * log(rect.width);

			//_pConfig_obj->m_Extentions.accelerator.interface_type

			if((size_t)detection->get_class_id() == 1 && (greencone.empty() || greencone.tl().y < bbox_min.y)) greencone = rect;
			if((size_t)detection->get_class_id() == 2 && (redcone.empty() || redcone.tl().y < bbox_min.y)) redcone = rect;

			/*writer.StartObject();
			writer.String("class");
			writer.Int((size_t)detection->get_class_id());
			writer.String("confidence");
			writer.Int(detection->get_confidence());
			writer.String("box");
			writer.StartArray();
			writer.Int(bbox_min.x);
			writer.Int(bbox_min.y);
			writer.Int(bbox_max.x);
			writer.Int(bbox_max.y);
			writer.EndArray();
			writer.EndObject();*/
		}
	}
	
	//std::cout << "bottomRectY :" << bottomRectY << "\n";
	
	 
	std::sort(centerPoint1.begin(), centerPoint1.end(), comparePoints);
	cv::polylines(*frame, centerPoint1, false, cv::Scalar(0, 255, 0), 2, 8, 0);
		
	std::sort(centerPoint2.begin(), centerPoint2.end(), comparePoints);
	cv::polylines(*frame, centerPoint2, false, cv::Scalar(0, 255, 0), 2, 8, 0);
	int center_point = (Rect1.x + Rect2.x) / 2;
	cv::line(*frame, cv::Point(center_point, 0), cv::Point(center_point, frame->size().width), cv::Scalar(0, 0, 255), 1, 8, 0);
	
	
	writer.StartObject();
	writer.String("center_point");
	writer.Int(center_point);
	writer.String("Direction");
	writer.Int(Direction);
	writer.EndObject();
	
    writer.EndArray();
	writer.EndObject();
	

	if (_pServer != NULL)
	{
		std::unordered_set<std::shared_ptr<WsServer::Connection>> connections = _pServer->get_connections();
		std::unordered_set<std::shared_ptr<WsServer::Connection>>::iterator iter = connections.begin();

		while (iter != connections.end()) {
			std::shared_ptr<WsServer::Connection> connection = *iter;
			if (connection->IsRequestMetadata()) {
				// metadata send to client.
				connection->send(ss.GetString(), [](const SimpleWeb::error_code &ec)
				{
					if(ec) {
						cout << "Server: Error sending message. " <<
							"Error: " << ec << ", error message: " << ec.message() << endl;
					}
				});
			}
            iter++;
		}
	}



}

#ifdef ______USE_NMS_HEF______

#else

void vstream_read_thread1_runner(hailo_output_vstream output_vstream, size_t output_vstream_frame_sizes)
{
	std::cout << "Starting vstream_read_thread1_runner\n";

	std::vector<uint8_t> dummydata(output_vstream_frame_sizes);

	while(!_thread_terminate)
	{
		hailo_status status = hailo_vstream_read_raw_buffer(output_vstream, dummydata.data(), output_vstream_frame_sizes);

        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed reading with status = " << status << std::endl;
            _terminate = _thread_terminate = true;
            break;
        }

        _m1.lock();
		_featuresbuffer[0].push(dummydata);
		_m1.unlock();

        // std::cout << "reading 1\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

	_thread1_terminated = true;
	std::cout << "vstream_read_thread1_runner Terminated\n";
}

void vstream_read_thread2_runner(hailo_output_vstream output_vstream, size_t output_vstream_frame_sizes)
{
	std::cout << "Starting vstream_read_thread2_runner\n";

	std::vector<uint8_t> dummydata(output_vstream_frame_sizes);

	while(!_thread_terminate)
	{
		hailo_status status = hailo_vstream_read_raw_buffer(output_vstream, dummydata.data(), output_vstream_frame_sizes);

        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed reading with status = " << status << std::endl;
            _terminate = _thread_terminate = true;
            break;
        }

        _m2.lock();
		_featuresbuffer[1].push(dummydata);
		_m2.unlock();

        // std::cout << "reading 2\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

	_thread2_terminated = true;
	std::cout << "vstream_read_thread2_runner Terminated\n";
}

void vstream_read_thread3_runner(hailo_output_vstream output_vstream, size_t output_vstream_frame_sizes)
{
	std::cout << "Starting vstream_read_thread3_runner\n";

	std::vector<uint8_t> dummydata(output_vstream_frame_sizes);

	while(!_thread_terminate)
	{
		hailo_status status = hailo_vstream_read_raw_buffer(output_vstream, dummydata.data(), output_vstream_frame_sizes);

        if (HAILO_SUCCESS != status)
        {
            std::cerr << "Failed reading with status = " << status << std::endl;
            _terminate = _thread_terminate = true;
            break;
        }

        _m3.lock();
		_featuresbuffer[2].push(dummydata);
		_m3.unlock();

        // std::cout << "reading 3\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

	_thread3_terminated = true;
	std::cout << "vstream_read_thread3_runner Terminated\n";
}

void yolo_output_collecter_thread_runner(hailo_vstream_info_t * output_vstream_info)
{
	std::cout << "Starting output_collecter_thread_runner\n";

    std::array<std::vector<uint8_t>, INFERNCE_MAX_OUTPUT_COUNT> featuresdata;

    while(!_thread_terminate)
	{
		if(_featuresbuffer[0].size() > 0 && _featuresbuffer[1].size() > 0 && _featuresbuffer[2].size() > 0)
		{
			_m1.lock();

			featuresdata[0] = _featuresbuffer[0].front();
			_featuresbuffer[0].pop();

			_m1.unlock();

			_m2.lock();

			featuresdata[1] = _featuresbuffer[1].front();
			_featuresbuffer[1].pop();

			_m2.unlock();

			_m3.lock();

			featuresdata[2] = _featuresbuffer[2].front();
			_featuresbuffer[2].pop();

			_m3.unlock();

			_fps++;

			HailoROIPtr roi = std::make_shared<HailoROI>(HailoROI(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f)));

			roi->add_tensor(std::make_shared<HailoTensor>(reinterpret_cast<uint8_t*>(featuresdata[2].data()), output_vstream_info[2]));
			roi->add_tensor(std::make_shared<HailoTensor>(reinterpret_cast<uint8_t*>(featuresdata[1].data()), output_vstream_info[1]));
			roi->add_tensor(std::make_shared<HailoTensor>(reinterpret_cast<uint8_t*>(featuresdata[0].data()), output_vstream_info[0]));

			yolov5(roi, _init_params);

			_frame_detections.push(roi);
        }
        else
		{
			usleep(1); // waiting for inference done
		}

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

	std::cout << "yolo_output_collecter_thread_runner Terminated\n";
}

#endif

unsigned long GetTick()
{
	struct timespec tp;

	clock_gettime(CLOCK_MONOTONIC,&tp);
	return (unsigned long)(tp.tv_sec *1000 + tp.tv_nsec / 1000000);
}

void intHandler(int dummy)
{
	_terminate = true;
}

bool parse_json(Document &doc, const std::string &jsonData)
{
    if (doc.Parse(jsonData.c_str()).HasParseError())
    {
        return false;
    }

    return doc.IsObject();

    // rapidjson::ParseResult result = doc.Parse(jsonData.c_str());
    // if (result.IsError())
    // printf( "RapidJson parse error: %s (%lu)\n", rapidjson::GetParseError_En(result.Code()), result.Offset());
    // return !result.IsError();
}

bool check_message_members(Document &doc, std::string &no_members)
{
    if (doc.HasMember("module") == false)
    {
        no_members += "module";
    }
    if (doc.HasMember("method") == false)
    {
        if (no_members.size() > 0)
            no_members += ",";
        no_members += "method";
    }
    if (doc.HasMember("cmd") == false)
    {
        if (no_members.size() > 0)
            no_members += ",";
        no_members += "cmd";
    }
    if (doc.HasMember("attr") == false)
    {
        if (no_members.size() > 0)
            no_members += ",";
        no_members += "attr";
    }
    if (doc.HasMember("payload") == false)
    {
        if (no_members.size() > 0)
            no_members += ",";
        no_members += "payload";
    }

    if (no_members.size() > 0)
        return false;

    return true;
}

bool process_message(shared_ptr<WsServer::Connection>& connection, Document &doc, std::string &resp_message)
{
    for (Value::ConstMemberIterator itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr)
        printf("Type of member %s\n", itr->name.GetString());

    std::string no_members = "";

    if (check_message_members(doc, no_members) == false)
    {
        std::cout << "no_members: " << no_members.c_str() << std::endl;
        if (no_members.size() > 0)
            resp_message = "{\"sc\":400, \"payload\":\"Bad request, there is no members(" + no_members + ").\"}";
        else
            resp_message = "{\"sc\":400, \"payload\":\"Bad request, there is no members.\"}";

        return false;
    }

    std::string member_module = doc["module"].GetString();
    std::string member_method = doc["method"].GetString();
    std::string member_cmd = doc["cmd"].GetString();
    std::string member_attr = doc["attr"].GetString();
    if (member_module.compare(MSG_MODULE_TYPE_ZAIV) == 0)
    {
        if (member_method.compare(MSG_METHOD_TYPE_POST) == 0)
        {
            if (member_cmd.compare(MSG_COMMAND_TYPE_CONFIG) == 0)
            {
                if (member_attr.compare(MSG_ATTRIBUTE_TYPE_CLASS) == 0)
                {
                    if (doc["payload"].IsArray())
                    {
                        for (size_t sz = 0; sz < doc["payload"].Size(); sz++)
                        {
                            if (doc["payload"][sz].IsObject() == false)
                            {
                                resp_message = "{\"sc\":400, \"payload\":\"Bad request, invalid payload.\"}";
                                return false;
                            }
                            if (doc["payload"][sz].IsArray())
                            {
                                resp_message = "{\"sc\":400, \"payload\":\"Bad request, invalid payload.\"}";
                                return false;
                            }
                            if (doc["payload"][sz].HasMember("index") &&
                                doc["payload"][sz].HasMember("name") &&
                                doc["payload"][sz].HasMember("show"))
                            {
                                int index = doc["payload"][sz]["index"].GetInt();
                                std::string name = doc["payload"][sz]["name"].GetString();
                                bool show = (bool)doc["payload"][sz]["show"].GetBool();

                                if (index < _pConfig_obj->m_Ai.model.classes.size())
                                {
                                    auto iter = _pConfig_obj->m_Ai.model.classes.begin();
                                    std::advance(iter, index);

                                    (*iter)->index = index;
                                    (*iter)->name = name;
                                    (*iter)->show = show;

                                    resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"class\", \"sc\":200, \"payload\":\"Ok\"}";
                                    return true;
                                }
                                else
                                {
                                    std::cout << "index error" << std::endl;
                                    resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"class\", \"sc\":400, \"payload\":\"Bad request, Invalid a payload format.\"}";
                                    return false;
                                }
                            }
                            else
                            {
                                std::cout << "member error" << std::endl;
                                resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"class\", \"sc\":400, \"payload\":\"Bad request, Invalid a payload format.\"}";
                                return false;
                            }
                        }
                    }
                    else
                    {
                        std::cout << "payload is not array" << std::endl;
                        resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"class\", \"sc\":400, \"payload\":\"Bad request, Invalid a payload format.\"}";
                        return false;
                    }
                }
                else if (member_attr.compare(MSG_ATTRIBUTE_TYPE_CONFIDENCE) == 0)
                {
                    if (doc["payload"].HasMember("confidence") && doc["payload"]["confidence"].IsDouble())
                    {
                        _pConfig_obj->m_Ai.inference.confidence = doc["payload"]["confidence"].GetDouble();
                        resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"confidence\", \"sc\":200, \"payload\":\"Ok\"}";
                        return true;
                    }
                    else {
                        resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"confidence\", \"sc\":400, \"payload\":\"Bad request, Invalid a payload format.\"}";
                        return false;
                    }
                }

                else if (member_attr.compare(MSG_ATTRIBUTE_TYPE_CAR) == 0)
                {
                    if (doc["payload"].HasMember("ref_center") && doc["payload"]["ref_center"].IsInt())
                    {
                        _pConfig_obj->m_extentions.car.controls.ref_center = doc["payload"]["ref_center"].GetInt();

                        cout << "_pConfig_obj->m_extentions.car.controls.ref_center: " << _pConfig_obj->m_extentions.car.controls.ref_center << endl;
                        resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"car\", \"sc\":200, \"payload\":\"Ok\"}";
                        return true;
                    }
                    if (doc["payload"].HasMember("deice_path") && doc["payload"]["deice_path"].IsString())
                    {
                        _pConfig_obj->m_extentions.car.device_path = doc["payload"]["deice_path"].GetString();

                        cout << "_pConfig_obj->m_extentions.car.device_path: " << _pConfig_obj->m_extentions.car.device_path.c_str() << endl;
                        resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"car\", \"sc\":200, \"payload\":\"Ok\"}";
                        return true;
                    }
                    // else
                    // {
                    //     resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"config\",\"attr\":\"car\", \"sc\":400, \"payload\":\"Bad request, Invalid a payload format.\"}";
                    //     return false;
                    // }
                }
            }
            else if (member_cmd.compare(MSG_COMMAND_TYPE_SUBSCRIBE) == 0)
            {
                if (member_attr.compare(MSG_ATTRIBUTE_TYPE_INFER_METADATA) == 0)
                {
                    connection->SetRequestMetadata(true);
                    resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"subscribe\",\"attr\":\"infer-metadata\", \"sc\":200, \"payload\":\"Ok\"}";
                    return true;
                }
                else
                {
                    resp_message = "{\"sc\":400, \"payload\":\"Bad request, Unsupported attribute.\"}";
                    return false;
                }
            }
            else if (member_cmd.compare(MSG_COMMAND_TYPE_UNSUBSCRIBE) == 0)
            {
                if (member_attr.compare(MSG_ATTRIBUTE_TYPE_INFER_METADATA) == 0)
                {
                    connection->SetRequestMetadata(false);
                    resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"subscribe\",\"attr\":\"infer-metadata\", \"sc\":200, \"payload\":\"Ok\"}";
                    return true;
                }
                else
                {
                    resp_message = "{\"sc\":400, \"payload\":\"Bad request, Unsupported attribute.\"}";
                    return false;
                }
            }
            else if (member_cmd.compare(MSG_COMMAND_TYPE_MOVE) == 0)
            {
                if (member_attr.compare(MSG_ATTRIBUTE_TYPE_CAR) == 0)
                {
                    //_car_move_value

                    if ((doc["payload"].HasMember("type") && doc["payload"]["type"].IsString()) &&
                        (doc["payload"].HasMember("left") && doc["payload"]["left"].IsInt()) &&
                        (doc["payload"].HasMember("right") && doc["payload"]["right"].IsInt()))
                    {
                        std::string strType = doc["payload"]["type"].GetString();
                        int typeIndex = -1;

                        if (strType.compare("straight") == 0)
                        {

                            _pConfig_obj->m_extentions.car.controls.pwm_stratight_gd = doc["payload"]["left"].GetInt();
                            _pConfig_obj->m_extentions.car.controls.pwm_stratight_god = doc["payload"]["right"].GetInt();
                        }
                        else if (strType.compare("turn") ==0 )
                        {
                            _pConfig_obj->m_extentions.car.controls.pwm_turn_gd = doc["payload"]["left"].GetInt();
                            _pConfig_obj->m_extentions.car.controls.pwm_turn_god = doc["payload"]["right"].GetInt();
                        }
                        else {
                            resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"move\",\"attr\":\"car\", \"sc\":400, \"payload\":\"Bad request, Invalid a payload format.\"}";
                            return false;
                        }

                        /*if (typeIndex == 0) {
                            if ((_pConfig_obj->m_extentions.car.controls.pwm_stratight_gd == 0) &&
                                (_pConfig_obj->m_extentions.car.controls.pwm_stratight_god == 0))
                            {
                                send_command("move", 0, 0);
                            }
                        }*/

                        cout << "strType: " << strType << endl;
                        cout << "_pConfig_obj->m_extentions.car.controls.pwm_stratight_gd: " << _pConfig_obj->m_extentions.car.controls.pwm_stratight_gd << endl;
                        cout << "_pConfig_obj->m_extentions.car.controls.pwm_stratight_god: " << _pConfig_obj->m_extentions.car.controls.pwm_stratight_god << endl;

                        cout << "_pConfig_obj->m_extentions.car.controls.pwm_turn_gd: " << _pConfig_obj->m_extentions.car.controls.pwm_turn_gd << endl;
                        cout << "_pConfig_obj->m_extentions.car.controls.pwm_turn_god: " << _pConfig_obj->m_extentions.car.controls.pwm_turn_god << endl;

                        resp_message = "{\"module\":\"zaiv\",\"method\":\"post\",\"cmd\":\"move\",\"attr\":\"car\", \"sc\":200, \"payload\":\"Ok\"}";
                        return true;
                    }
                }
                else
                {
                    resp_message = "{\"sc\":400, \"payload\":\"Bad request, Unsupported attribute.\"}";
                    return false;
                }
            }
            else if (member_cmd.compare(MSG_COMMAND_TYPE_STOP) == 0)
            {
                // if (member_attr.compare(MSG_ATTRIBUTE_TYPE_CAR) == 0)
                // {
                //     // _pConfig_obj->m_extentions.car.controls.pwm_stratight_gd = 0;
                //     // _pConfig_obj->m_extentions.car.controls.pwm_stratight_god = 0;

                //     // send_command("move",0,0);
                // }
            }
            else
            {
                resp_message = "{\"sc\":400, \"payload\":\"Bad request, Unsupported command.\"}";
                return false;
            }
        }

        resp_message = "{\"sc\":400, \"payload\":\"Bad request, failed to process message.\"}";
        return false;
    }
    else if (member_module.compare(MSG_MODULE_TYPE_VIDEO) == 0)
    {
    }
    else if (member_module.compare(MSG_MODULE_TYPE_ACTION) == 0)
    {
    }


    return true;
}

bool load_configuration()
{
    if (_pConfig_obj == NULL)
    {
        _pConfig_obj = new ZaivCfg();
    }

    if (_pConfig_obj == NULL)
    {
        std::cout << "Failed to allocate configuration object." << std::endl;
        return false;
    }

    if (_pConfig_obj->DeserializeFromFile(CONFIGURATION_FN) == false)
    {
        std::cout << "Failed to load file" << std::endl;
        return false;
    }

    std::cout << "accelerator type: " << _pConfig_obj->m_Ai.accelerator.type.c_str() << std::endl;

    return true;
}

bool save_configuration()
{
    if (_pConfig_obj == NULL)
    {
        std::cout << "There is no configuration object." << std::endl;
        return false;
    }

    if (_pConfig_obj->SerializeToFile(CONFIGURATION_FN) == false)
    {
        std::cout << "Failed to save file" << std::endl;
        return false;
    }

    return true;
}

bool init_video(cv::VideoCapture *pVideo)
{
    *pVideo = cv::VideoCapture(0);
    pVideo->set(cv::CAP_PROP_FRAME_WIDTH, 640);
    pVideo->set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    if (!pVideo->isOpened())
    {
        std::cout << "camera 0 is not vaild\n";

        *pVideo = cv::VideoCapture("../samples/corns.mov");

        if (!pVideo->isOpened())
        {
            std::cout << "input source not valid\n";
            return false;
        }
    }
    return true;
}

bool init_ws_server(WsServer* pServer)
{
    pServer->config.port = WS_SERVER_PORT;

    auto &zaiv_ws_protocol_v1 = pServer->endpoint["^/protocol/v1/?$"];

    zaiv_ws_protocol_v1.on_message = [](shared_ptr<WsServer::Connection> connection, shared_ptr<WsServer::InMessage> in_message)
    {
        std::string message = in_message->string();
        std::string resp_message = "";

        std::cout << "Server: Message received: \"" << message.c_str() << "\" from " << connection.get() << std::endl;
        Document doc;
        if (parse_json(doc, message.c_str()))
        {
            if (process_message(connection, doc, resp_message)) {
                std::cout << "Successful processed message." << std::endl;
            }
            else{
                std::cout<< "Failed to process message" << std::endl;
            }
        }
        else {
            resp_message = "{\"sc\":400, \"payload\":\"Bad request, Invalid Json Structure).\"}";
            std::cout<< "Invalid Json structure!" << std::endl;
        }

        if (resp_message.size() > 0) {
            connection->send(resp_message, [](const SimpleWeb::error_code &ec)
            {
                if(ec) {
                    cout << "Server: Error sending message. " <<
                        "Error: " << ec << ", error message: " << ec.message() << endl;
                }
            });
        }
    };

    zaiv_ws_protocol_v1.on_open = [](shared_ptr<WsServer::Connection> connection)
    {
        std::cout<< "Server: Opened connection " << connection.get() << std::endl;
    };

    // See RFC 6455 7.4.1. for status codes
    zaiv_ws_protocol_v1.on_close = [](shared_ptr<WsServer::Connection> connection, int status, const string & /*reason*/)
    {
        std::cout<< "Server: Closed connection " << connection.get() << " with status code " << status << std::endl;
    };

    // Can modify handshake response headers here if needed
    zaiv_ws_protocol_v1.on_handshake = [](shared_ptr<WsServer::Connection> /*connection*/, SimpleWeb::CaseInsensitiveMultimap & /*response_header*/)
    {
        return SimpleWeb::StatusCode::information_switching_protocols; // Upgrade to websocket
    };

    // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
    zaiv_ws_protocol_v1.on_error = [](shared_ptr<WsServer::Connection> connection, const SimpleWeb::error_code &ec)
    {
        std::cout<< "Server: Error in connection " << connection.get() << ". "
             << "Error: " << ec << ", error message: " << ec.message() << std::endl;
    };

    return true;
}
void ws_thread_runner(WsServer *pServer)
{
    if (pServer == NULL)
    {
        std::cout<< "Server: failed to start server. Server object is null.";
        return;
    }

    pServer->start();
}


bool MakeudpsinkWriter(cv::VideoWriter *videowrite, int w, int h)
{
	// HW Encode RTSP H264
    std::cout << "------------------------------------------------1\n";
	std::string pipeline_str = "appsrc ! videoconvert ! avenc_mpeg1video bitrate=4000000 ! mpegtsmux ! tcpclientsink host=localhost port=8888";
 //   std::string pipeline_str = "appsrc ! videoconvert ! v4l2h264enc ! video/x-h264,level=(string)4 ! h264parse ! rtspclientsink location=rtsp://localhost:8554/mystream";

    std::cout << "------------------------------------------------2\n";
	videowrite->open(pipeline_str, 0, 30, cv::Size(w,h), true);
	std::cout << "------------------------------------------------3\n";
	if(!videowrite->isOpened())
	{
		std::cout << "videowrite open failed\n";
		return false;
	}

	return true;
}


#ifdef ______USE_NMS_HEF______

void nms_hef_draw_all(cv::Mat * frame, HailoROIPtr roi)
{
	for (HailoObjectPtr obj : roi->get_objects())
	{
		if (obj->get_type() == HAILO_DETECTION)
		{
			HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);

			cv::Scalar color = get_color((size_t)detection->get_class_id());
			std::string text = get_detection_text(detection, show_confidence);

			HailoBBox roi_bbox = hailo_common::create_flattened_bbox(roi->get_bbox(), roi->get_scaling_bbox());
			auto detection_bbox = detection->get_bbox();

			auto bbox_min = cv::Point(
				((detection_bbox.xmin() * roi_bbox.width()) + roi_bbox.xmin()) * frame->size().width,
				((detection_bbox.ymin() * roi_bbox.height()) + roi_bbox.ymin()) * frame->size().height);
			auto bbox_max = cv::Point(
				((detection_bbox.xmax() * roi_bbox.width()) + roi_bbox.xmin()) * frame->size().width,
				((detection_bbox.ymax() * roi_bbox.height()) + roi_bbox.ymin()) * frame->size().height);

			cv::Rect rect = cv::Rect(bbox_min, bbox_max);
			cv::rectangle(*frame, rect, color);

			// Draw text
			cv::Point text_position = cv::Point(rect.x - log(rect.width), rect.y - log(rect.width));
			float font_scale = TEXT_FONT_FACTOR * log(rect.width);
			cv::putText(*frame, text, text_position, 0, font_scale, color);

		}
	}
}





static std::map<uint8_t, std::string> track0110_labels = {
	{0, "unlabeled"},
	{1, "cone_one"},
	{2, "cone_two"}
};

void vstream_read_thread1_runner(hailo_output_vstream output_vstream, size_t output_vstream_frame_sizes, hailo_vstream_info_t* output_vstream_info)
{
	std::cout << "Starting vstream_read_thread1_runner\n";

	std::vector<uint8_t> dummydata(output_vstream_frame_sizes);

	while (!_thread_terminate)
	{
		if (_thread_terminate) std::cout << "read1 waiting\n";
		hailo_status status = hailo_vstream_read_raw_buffer(output_vstream, dummydata.data(), output_vstream_frame_sizes);

		if (HAILO_SUCCESS != status) {
			std::cerr << "Failed reading with status = " << status << std::endl;
			_terminate = _thread_terminate = true;
			break;
		}


		_fps++;


		HailoROIPtr roi = std::make_shared<HailoROI>(HailoROI(HailoBBox(0.0f, 0.0f, 1.0f, 1.0f)));

		roi->add_tensor(std::make_shared<HailoTensor>(reinterpret_cast<uint8_t*>(dummydata.data()), output_vstream_info[0]));

		auto post = MobilenetSSDPost(roi->get_tensor("track_kss/nms1"), track0110_labels);
		auto detections = post.decode();
		hailo_common::add_detections(roi, detections);

		_frame_detections.push(roi);

	}

	_thread1_terminated = true;
	std::cout << "vstream_read_thread1_runner Terminated\n";
}

#else


#endif




/////////////////////////////////////////////////////////////////////////////
//                                  Main                                   //
/////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
	std::cout << argv[0] << " Started" << std::endl;


    signal(SIGINT, intHandler);
    std::thread ws_thread;
	std::thread vstream_read_thread1;
	std::thread vstream_read_thread2;
	std::thread vstream_read_thread3;
	std::thread yolo_output_collect_thread;
    cv::VideoCapture video;

    if (!load_configuration()) {
        std::cout << "Server: failed to load configuration" << std::endl;
        return 0;
    }

    //connect_Serial();

    if (_pConfig_obj->m_Ai.accelerator.interface_type.compare("ethernet") == 0) {
        system("../configure_ethernet_buffers.sh eth0");
        std::string str_fw_c_reset = "hailortcli fw-control reset --reset-type 0 --ip ";
        str_fw_c_reset.append(_pConfig_obj->m_Ai.accelerator.ip.c_str());
		system(str_fw_c_reset.c_str());
		sleep(2);
        // DEFAULT_HEF_FILEs
        std::string str_configure_eth = "hailortcli udp-rate-limiter autoset --hef ";
        str_configure_eth.append(DEFAULT_HEF_FILE);
        str_configure_eth.append(" --fps 60 --interface-name ");
        str_configure_eth.append("\"");
        str_fw_c_reset.append(_pConfig_obj->m_Ai.accelerator.interface_name.c_str());
        str_configure_eth.append("\"");
        system(str_configure_eth.c_str());
    }

    _pServer = new WsServer();

    if (_pServer == NULL) {
        std::cout<< "Server: failed to create server" << std::endl;
        return 0;
    }

    // init video using camera0 or sample video.
    if (init_video(&video) == false)
        return 0;

    // rtsp
	int videowrite_w = video.get(cv::CAP_PROP_FRAME_WIDTH);
	int videowrite_h = video.get(cv::CAP_PROP_FRAME_HEIGHT);
	cv::VideoWriter videowrite;
	if(!MakeudpsinkWriter(&videowrite, 512, 512))
        return 0;

    // WebSocket (WS)-server at port WS_SERVER_PORT using 1 thread
    if (init_ws_server(_pServer) == false)
        return 0;

    cv::Mat scaledframe;
	cv::Mat rgbframe;
    cv::Mat frame(_pConfig_obj->m_Ai.inference.image_size[WIDTH], _pConfig_obj->m_Ai.inference.image_size[HEIGHT], CV_8UC3);

	hailo_status status = HAILO_UNINITIALIZED;
	hailo_device device = NULL;
	hailo_hef hef = NULL;
	hailo_configure_params_t config_params = { 0 };
	hailo_configured_network_group network_group = NULL;
	size_t network_group_size = 1;
    hailo_input_vstream_params_by_name_t input_vstream_params[INFERNCE_MAX_INPUT_COUNT] = {0};
    hailo_output_vstream_params_by_name_t output_vstream_params[INFERNCE_MAX_OUTPUT_COUNT] = {0};
    size_t input_vstreams_size = INFERNCE_MAX_INPUT_COUNT;
	size_t output_vstreams_size = INFERNCE_MAX_OUTPUT_COUNT;
	hailo_activated_network_group activated_network_group = NULL;
	hailo_input_vstream input_vstreams[INFERNCE_MAX_INPUT_COUNT] = { NULL };
	hailo_output_vstream output_vstreams[INFERNCE_MAX_OUTPUT_COUNT] = { NULL };
	size_t input_vstream_frame_sizes[INFERNCE_MAX_INPUT_COUNT], output_vstream_frame_sizes[INFERNCE_MAX_OUTPUT_COUNT];
	hailo_vstream_info_t input_vstream_info[INFERNCE_MAX_INPUT_COUNT];
	hailo_vstream_info_t output_vstream_info[INFERNCE_MAX_OUTPUT_COUNT];
	std::vector<uint8_t> input_dummydata;
	int hef_input_width;
	int hef_input_height;


    if (_pConfig_obj->m_Ai.accelerator.interface_type.compare("ethernet") == 0) {


        hailo_eth_device_info_t device_infos[ACCELERATOR_SCAN_MAX_DEVICE_DEFAULT];

        size_t num_of_devices = 0;

        status = hailo_scan_ethernet_devices(_pConfig_obj->m_Ai.accelerator.interface_name.c_str(),
                                                device_infos,
                                                ACCELERATOR_SCAN_MAX_DEVICE_DEFAULT,
                                                &num_of_devices,
                                                ACCELERATOR_SCAN_TIMEOUT_DEFAULT);
        REQUIRE_SUCCESS(status, l_exit, "Failed to scan eth_device");

        status = hailo_create_ethernet_device(device_infos, &device);
        REQUIRE_SUCCESS(status, l_exit, "Failed to create eth_device");

        std::cout << "num_of_devices : " << num_of_devices << std::endl;

        status = hailo_create_hef_file(&hef, DEFAULT_HEF_FILE);
        REQUIRE_SUCCESS(status, l_release_device, "Failed reading hef file");

        status = hailo_init_configure_params(hef, HAILO_STREAM_INTERFACE_ETH, &config_params);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed initializing configure parameters");

    }
    else {

        status = hailo_create_pcie_device(NULL, &device);
        REQUIRE_SUCCESS(status, l_exit, "Failed to create pcie_device");

        status = hailo_create_hef_file(&hef, DEFAULT_HEF_FILE);
        REQUIRE_SUCCESS(status, l_release_device, "Failed reading hef file");

        status = hailo_init_configure_params(hef, HAILO_STREAM_INTERFACE_PCIE, &config_params);
        REQUIRE_SUCCESS(status, l_release_hef, "Failed initializing configure parameters");
    }

	status = hailo_configure_device(device, hef, &config_params, &network_group, &network_group_size);
	REQUIRE_SUCCESS(status, l_release_hef, "Failed configure devcie from hef");
	REQUIRE_ACTION(network_group_size == 1, status = HAILO_INVALID_ARGUMENT, l_release_hef, "Invalid network group size");

	status = hailo_make_input_vstream_params(network_group, true, HAILO_FORMAT_TYPE_AUTO, input_vstream_params, &input_vstreams_size);
	REQUIRE_SUCCESS(status, l_release_hef, "Failed making input virtual stream params");

	status = hailo_make_output_vstream_params(network_group, true, HAILO_FORMAT_TYPE_AUTO, output_vstream_params, &output_vstreams_size);
	REQUIRE_SUCCESS(status, l_release_hef, "Failed making output virtual stream params");

	std::cout << "input_vstreams_size : " << input_vstreams_size << std::endl;
	std::cout << "output_vstreams_size : " << output_vstreams_size << std::endl;

	REQUIRE_ACTION(((input_vstreams_size == INFERNCE_MAX_INPUT_COUNT) || (output_vstreams_size == INFERNCE_MAX_OUTPUT_COUNT)), status = HAILO_INVALID_OPERATION, l_release_hef, "Expected one input vstream and three outputs vstreams");

	status = hailo_create_input_vstreams(network_group, input_vstream_params, input_vstreams_size, input_vstreams);
	REQUIRE_SUCCESS(status, l_release_hef, "Failed creating input virtual streams");

	status = hailo_create_output_vstreams(network_group, output_vstream_params, output_vstreams_size, output_vstreams);
	REQUIRE_SUCCESS(status, l_release_input_vstream, "Failed creating output virtual streams");

	status = hailo_activate_network_group(network_group, NULL, &activated_network_group);
	REQUIRE_SUCCESS(status, l_release_output_vstream, "Failed activating network group");

	for (size_t i = 0; i < input_vstreams_size; i++)
	{
		status = hailo_get_input_vstream_frame_size(input_vstreams[i], &input_vstream_frame_sizes[i]);
		REQUIRE_SUCCESS(status, l_release_output_vstream, "Failed getting input virtual stream frame size");

		std::cout << "input_vstream_frame_sizes[" << i << "] : " << input_vstream_frame_sizes[i] << std::endl;

		status = hailo_get_input_vstream_info(input_vstreams[i], &input_vstream_info[i]);
		REQUIRE_SUCCESS(status, l_deactivate_network_group, "Failed to get input vstream info");
	}

	hef_input_width = input_vstream_info[0].shape.width;
	hef_input_height = input_vstream_info[0].shape.height;

	std::cout << "input width: " << hef_input_width << ", height: " << hef_input_height << std::endl;

	for (size_t i = 0; i < output_vstreams_size; i++)
	{
		status = hailo_get_output_vstream_frame_size(output_vstreams[i], &output_vstream_frame_sizes[i]);
		REQUIRE_SUCCESS(status, l_release_output_vstream, "Failed getting output virtual stream frame size");

		std::cout << "output_vstream_frame_sizes[" << i << "] : " << output_vstream_frame_sizes[i] << std::endl;

		status = hailo_get_output_vstream_info(output_vstreams[i], &output_vstream_info[i]);
		REQUIRE_SUCCESS(status, l_release_output_vstream, "Failed to get output vstream info");

	}

	input_dummydata = std::vector<uint8_t>(input_vstream_frame_sizes[0]);

	#ifdef ______USE_NMS_HEF______

	vstream_read_thread1 = std::thread(vstream_read_thread1_runner, output_vstreams[0], output_vstream_frame_sizes[0], output_vstream_info);


	#else

	_init_params = init("./yolov5.json", "yolov5");

	vstream_read_thread1 = std::thread(vstream_read_thread1_runner, output_vstreams[0], output_vstream_frame_sizes[0]);
	vstream_read_thread2 = std::thread(vstream_read_thread2_runner, output_vstreams[1], output_vstream_frame_sizes[1]);
	vstream_read_thread3 = std::thread(vstream_read_thread3_runner, output_vstreams[2], output_vstream_frame_sizes[2]);

	yolo_output_collect_thread = std::thread(yolo_output_collecter_thread_runner, output_vstream_info);

	#endif

    ws_thread = std::thread(ws_thread_runner, _pServer);

    while (true)
	{
		if (_frame_detections.size() < 1)
		{
			if (video.read(frame))
			{
				cv::Mat* mat;

                if (frame.size().width == _pConfig_obj->m_Ai.inference.image_size[WIDTH] && frame.size().height == _pConfig_obj->m_Ai.inference.image_size[HEIGHT])
                {
					mat = &frame;
				}
				else
				{
                    cv::resize(frame, scaledframe, cv::Size(_pConfig_obj->m_Ai.inference.image_size[WIDTH], _pConfig_obj->m_Ai.inference.image_size[HEIGHT]), 0, 0);
                    mat = &scaledframe;
				}

				_frame_queue.push((*mat).clone());

				cv::cvtColor(*mat, rgbframe, cv::COLOR_BGR2RGB);

				hailo_status status = hailo_vstream_write_raw_buffer(input_vstreams[0], rgbframe.data, input_vstream_frame_sizes[0]);
				if (HAILO_SUCCESS != status)
				{
					std::cerr << "Failed writing to device data of image. Got status = " << status << std::endl;
					_terminate = true;
					break;
				}

			}
			else
			{
				break;
			}
		}

        static unsigned long tick_past = 0;
        static unsigned long tick_past_serial = 0;
        if (tick_past == 0)
		{
			tick_past = GetTick();
		}
		else if (tick_past + 1000 < GetTick())
		{
			tick_past = GetTick();

			std::cout << "fps : " << _fps << std::endl;
			_fps = 0;
		}

        // if (tick_past_serial + 100 < GetTick())
        // {
        //     tick_past_serial = GetTick();
        //     // cout << "_car_move_value[0][0]: " << _car_move_value[0][0] << endl;
        //     // cout << "_car_move_value[0][1]: " << _car_move_value[0][1] << endl;
        //     //if ((_pConfig_obj->m_extentions.car.controls.pwm_stratight_gd != 0) && (_pConfig_obj->m_extentions.car.controls.pwm_stratight_god != 0)) {
        //         int move_type = 0;
        //         if ((_car_diff_value[0] < 0) && (_car_diff_value[1] > 0))
        //         {
        //             send_command("move", _pConfig_obj->m_extentions.car.controls.pwm_turn_gd, _pConfig_obj->m_extentions.car.controls.pwm_turn_god);
        //         }
        //         else {
        //             send_command("move", _pConfig_obj->m_extentions.car.controls.pwm_stratight_gd, _pConfig_obj->m_extentions.car.controls.pwm_stratight_god);
        //         }
        //     //}

        //     std::string resp = read_command();
        //     std::cout << "read: " << resp.c_str() << std::endl;

        //     // if (resp.size() == 0)
        //     // {
        //     //     close(uart0_filestream);
        //     //     uart0_filestream = -1;

        //     // }
        // }

		if (!_frame_detections.empty())
		{
			cv::Mat& showframe = _frame_queue.front();
			HailoROIPtr roi = _frame_detections.front();


			std::vector<HailoDetectionPtr> detections = hailo_common::get_hailo_detections(roi);

			// std::cout << "detection counts: " << detections.size() << "\n";
			/*
			for (auto &detection : detections)
			{
				std::cout << "class id    : " << detection->get_class_id() << "\n";
				std::cout << "class label : " << detection->get_label() << "\n";
				std::cout << "confidence  : " << detection->get_confidence() << "\n";

				HailoBBox detection_bbox = detection->get_bbox();
				std::cout << "xmin        : " << detection_bbox.xmin() << "\n";
				std::cout << "ymin        : " << detection_bbox.ymin() << "\n";
				std::cout << "width       : " << detection_bbox.width() << "\n";
				std::cout << "height      : " << detection_bbox.height() << "\n";
			}*/



			std::list<Classes*> &classes = _pConfig_obj->m_Ai.model.classes;
			draw_all_send_data(&showframe, roi, classes);

			// cv::imshow("video", showframe);

            if(videowrite.isOpened()) videowrite.write(showframe);
			else
			{
				std::cout << "videoWrite open Failed\n";
				break;
			}

			_frame_detections.pop();
			_frame_queue.pop();
		}

		char c = cv::waitKey(1);
		if (_terminate)
		{
			std::cout << "_terminate EXIT ...........................\n";
			break;
		}
		if (c == 'q')
		{
			std::cout << "input EXIT ...........................\n";

			break;
		}
	}

	close(uart0_filestream);
    uart0_filestream = -1;

    if (_pConfig_obj != NULL) {
        delete _pConfig_obj;
        _pConfig_obj = NULL;
    }


    _thread_terminate = true;

	for (int i = 0; i < 10; i++)
	{
		usleep(1000 * 100);


		#ifdef ______USE_NMS_HEF______

		if(_thread1_terminated)
			break;

		#else

		if(_thread1_terminated && _thread2_terminated && _thread3_terminated)
			break;

		#endif

		std::cout << "Write dummy buffer for Exiting\n";
		hailo_vstream_write_raw_buffer(input_vstreams[0], input_dummydata.data(), input_vstream_frame_sizes[0]);

	}


	#ifdef ______USE_NMS_HEF______

	std::cout << "vstream_read_thread1 waiting\n";
	vstream_read_thread1.join();

	#else

	std::cout << "vstream_read_thread1 waiting\n";
	vstream_read_thread1.join();
	std::cout << "vstream_read_thread2 waiting\n";
	vstream_read_thread2.join();
	std::cout << "vstream_read_thread3 waiting\n";
	vstream_read_thread3.join();

	#endif

    // pclose(ffmpeg_pipe);
    cv::destroyAllWindows();

    std::cout << "Releasing Hailo Resources\n";

    status = HAILO_SUCCESS;
	l_deactivate_network_group:
	(void)hailo_deactivate_network_group(activated_network_group);
	l_release_output_vstream:
	(void)hailo_release_output_vstreams(output_vstreams, output_vstreams_size);
	l_release_input_vstream:
	(void)hailo_release_input_vstreams(input_vstreams, input_vstreams_size);
	l_release_hef:
	(void)hailo_release_hef(hef);
	l_release_device:
	(void)hailo_release_device(device);
	l_exit:

    if (_pServer != NULL) {
		std::cout << "stop _pServer\n";
        _pServer->stop();
		std::cout << "delete _pServer\n";
        delete _pServer;
        _pServer = NULL;
    }

    if (_pConfig_obj != NULL) {
		std::cout << "Saving configurations...\n";
        save_configuration();
		std::cout << "delete _pConfig_obj\n";
        delete _pConfig_obj;
        _pConfig_obj = NULL;
    }

	usleep(1000 * 1000);
    // std::cout << "ws_server_thread waitting\n";
    // ws_thread.join();

	std::cout << argv[0] << " Terminated" << std::endl;
	return status;
}
