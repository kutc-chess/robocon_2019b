#include<ros/ros.h>
#include<three_omuni/button.h>
#include"motor_serial/motor_serial.h"

ros::ServiceClient shot;
motor_serial::motor_serial srv;

void controllerCallback(const three_omuni::button &button){
    static bool loading_flag_prev = false;
    static bool hand_flag_prev = false;
    if(button.loading){
        if(loading_flag_prev == false){
            srv.request.id = 3;
            srv.request.cmd = 34;
            srv.request.data = 1;
            loading_flag_prev = true;
        }else if(loading_flag_prev == true){
            srv.request.id = 3;
            srv.request.cmd = 34;
            srv.request.data = -1;
            loading_flag_prev = false;
        }
    }
    if(button.shooting){
        srv.request.id = 3;
        srv.request.cmd = 30;
        srv.request.data = 50;
    }
    if(button.hand){
        if(hand_flag_prev == false){
            srv.request.id = 3;
            srv.request.cmd = 40;
            srv.request.data = 90;
            hand_flag_prev = true;
        }else if(hand_flag_prev == true){
            srv.request.id = 3;
            srv.request.cmd = 40;
            srv.request.data = 180;
            hand_flag_prev = false;
        }
    }
}
int main(int argc, char **argv){
    ros::init(argc, argv, "shotting_cloths");
    ros::NodeHandle n;
    ros::Subscriber shot_sub = n.subscribe("controller_info", 10, controllerCallback);
    shot = n.serviceClient<motor_serial::motor_serial>("shotting_cloths");
    ros::spin();
    return 0;
}
