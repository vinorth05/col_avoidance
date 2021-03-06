//
// Simple example to demonstrate how to use the Dronecode SDK.
//
// Author: Julian Oes <julian@oes.ch>

#include <iostream> // std::cout, std::fixed
#include <chrono>
#include <ctime>
#include <cstdint>
#include <thread>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <iomanip> // std::setprecision

#include <dronecode_sdk/dronecode_sdk.h>            //change folder??
#include <dronecode_sdk/plugins/action/action.h>
#include <dronecode_sdk/plugins/telemetry/telemetry.h>
#include <dronecode_sdk/plugins/offboard/offboard.h>
#include "udp_client_server.h"

using namespace dronecode_sdk;
//using namespace std;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using std::chrono::seconds;
using namespace udp_client_server;

#define ERROR_CONSOLE_TEXT "\033[31m" // Turn text on console red
#define TELEMETRY_CONSOLE_TEXT "\033[34m" // Turn text on console blue
#define NORMAL_CONSOLE_TEXT "\033[0m" // Restore normal console colour


//Threshold Box: center area in Image Frame
#define X_THRESHOLD 0.2           // min = -1 and MAX = 1
#define Y_THRESHOLD 0.2
#define MAX_SIZE 1024


void usage(std::string bin_name)
{
    std::cout << NORMAL_CONSOLE_TEXT << "Usage : " << bin_name << " <connection_url>" << std::endl
              << "Connection URL format should be :" << std::endl
              << " For TCP : tcp://[server_host][:server_port]" << std::endl
              << " For UDP : udp://[bind_host][:bind_port]" << std::endl
              << " For Serial : serial:///path/to/serial/dev[:baudrate]" << std::endl
              << "For example, to connect to the simulator use URL: udp://:14540" << std::endl;
}

int sgn(double v)
{
    if (v < 0) return -1;
    if (v > 0) return 1;
    return 0;
}

//MAIN

int main(int argc, char **argv)
{
    DronecodeSDK        dc;
    std::string         connection_url;
    ConnectionResult    connection_result;

    //------------------------------<Connect to a Vehicle>------------------------------//
    //----------------------------------------------------------------------------------//

    udp_server* server = new udp_server("127.0.0.1", 8080);

    //bool discovered_system;
    if (argc == 2)
    {
        connection_url      =       argv[1];
        connection_result   =       dc.add_any_connection(connection_url);
    }
    else
    {
        usage(argv[0]);
        return 1;
    }

    if (connection_result != ConnectionResult::SUCCESS)
    {
        std::cout << ERROR_CONSOLE_TEXT
                  << "Connection failed: " << connection_result_str(connection_result)
                  << NORMAL_CONSOLE_TEXT << std::endl;
        return 1;
    }

    //added
    // Wait for the system to connect via heartbeat
    while (!dc.is_connected()) {
        std::cout << "Wait for system to connect via heartbeat" << std::endl;
        sleep_for(seconds(1));
    }

    // System got discovered.
    System &system =    dc.system();

//    std::cout << "Waiting to discover system..." << std::endl;
//    dc.register_on_discover([&discovered_system](uint64_t uuid) {
//        std::cout << "Discovered system with UUID: " << uuid << std::endl;
//        discovered_system = true;
//    });

//    // We usually receive heartbeats at 1Hz, therefore we should find a system after around 2
//    // seconds.
//    sleep_for(seconds(2));

//    if (!discovered_system) {
//        std::cout << ERROR_CONSOLE_TEXT << "No system found, exiting." << NORMAL_CONSOLE_TEXT
//                  << std::endl;
//        return 1;
//    }


    //----------------------------------------------------------------------------------//
    //----------------------------------------------------------------------------------//

    //-------------------------<Monitor Important Variables>----------------------------//

    auto action = std::make_shared<Action>(system);
    auto offboard = std::make_shared<Offboard>(system);
    auto telemetry = std::make_shared<Telemetry>(system);


    // We want to listen to the altitude of the drone at 1 Hz.
    const Telemetry::Result set_rate_result = telemetry->set_rate_position(1.0);
    if (set_rate_result != Telemetry::Result::SUCCESS) {
        std::cout << ERROR_CONSOLE_TEXT
                  << "Setting rate failed:" << Telemetry::result_str(set_rate_result)
                  << NORMAL_CONSOLE_TEXT << std::endl;
        return 1;
    }

    // Set up callback to monitor altitude while the vehicle is in flight
   // telemetry->position_async([](Telemetry::Position position) {
        //std::cout << TELEMETRY_CONSOLE_TEXT // set to blue
          //        << "Altitude: " << position.relative_altitude_m << " m"
            //      << NORMAL_CONSOLE_TEXT // set to default color again
              //    << std::endl;
    //});

    //----------------------------------------------------------------------------------//
    //----------------------------------------------------------------------------------//

    //--------------------------------<Arm and Takeoff>---------------------------------//
    // Check if vehicle is ready to arm
    while (telemetry->health_all_ok() != true) {
        std::cout << "Vehicle is getting ready to arm" << std::endl;
        sleep_for(seconds(1));
    }
    std::cout << "System is ready" << std::endl;

    // Arm vehicle
    std::cout << "Arming..." << std::endl;
    const Action::Result arm_result = action->arm();

    if (arm_result != Action::Result::SUCCESS) {
        std::cout << ERROR_CONSOLE_TEXT << "Arming failed:" << Action::result_str(arm_result)
                  << NORMAL_CONSOLE_TEXT << std::endl;
        return 1;
    }
    std::cout << "Armed" << std::endl;

    // Take off
    std::cout << "Taking off..." << std::endl;
    const Action::Result takeoff_result = action->takeoff();
    if (takeoff_result != Action::Result::SUCCESS) {
        std::cout << ERROR_CONSOLE_TEXT << "Takeoff failed:" << Action::result_str(takeoff_result)
                  << NORMAL_CONSOLE_TEXT << std::endl;
        return 1;
    }
    std::cout << "In Air..." << std::endl;

    // Let it hover for a bit before landing again.
    //sleep_for(seconds(5));
    //----------------------------------------------------------------------------------//
    //----------------------------------------------------------------------------------//

    //------------------------------<Variables of Interest>-----------------------------//

    bool detected = true;                 // Given by detection algorithm (not really now -> we only send velocity commands when we receive a detection output)
    bool net = false;                      // Given by listening on SERVO_OUTPUT_RAW message if Servo is plugged on PXH AND listening on the MAV_FRAME parameter (Quadcopter / Hexacopter)
    //bool track = false;                 // Given by Task control (this idea was given up -> be careful not to track our own drones...)
    //float l1;
    //float l2;                             // l1 and l2: tuning parameters for the velocity
    float n;
    float e;                              // velocity components in NED frame (n, e, d) in m/s
    float d;
    // + yaw_deg as 4th input : Yaw in degrees (0 North, positive is clock-wise looking from above)
    //float v_x;
    //float v_y;                            // velocity components in body frame (v_x, v_y, v_z) in m/s  -> forward / right / down
    //float v_z;
    // + yawspeed_deg_s as 4th input : Yaw angular rate in deg/s (positive for clock-wise looking from above)
    //const double pi = M_PI;
    Offboard::Result offboard_result;
    //float psi;
    bool hasStarted = false;
    bool isOffboard = false;
    //time_t current_time;

    //----------------------------------------------------------------------------------//
    //----------------------------------------------------------------------------------//
    //-------------------------------------<Algorithm>----------------------------------//

    // Start offboard mode.
    offboard->set_velocity_ned({0.0f, 0.0f, 0.0f, 0.0f});
    offboard_result = offboard->start();
    if (offboard_result != Offboard::Result::SUCCESS) {
        std::cerr << "Offboard::start() failed: "
        << Offboard::result_str(offboard_result) << std::endl;
        }
    if (offboard_result == Offboard::Result::SUCCESS) {
        std::cerr << "Offboard::start() success: "
        << Offboard::result_str(offboard_result) << std::endl;
        }

    //auto start_time = std::chrono::system_clock::now();  // start chrono (for testing)


    auto start_time = std::chrono::system_clock::now();
    while (1==1)   // here put a timer (10-15s?) for the test with real drones!!!
    {
        //std::cout << "Waiting for receive server input..." << std::endl;
        char* msg = new char[MAX_SIZE];

        int ret = server->timed_recv(msg, MAX_SIZE, 5000); //5000ms
        //std::cout << "ret" << ret << std::endl;
        //std::cout << "isOffboard" << isOffboard << std::endl; //false: 0
        if (ret == -1 && isOffboard == true)
        {
            //std::cout << "ret = -1" << std::endl;
            offboard_result = offboard->stop();  // STOP OFFBOARD MODE
            isOffboard = false;
            hasStarted = false;
        }

        else
        {
            if (hasStarted == false)
            {
                hasStarted = true;
                offboard->set_velocity_body({0.0f, 0.0f, 0.0f, 0.0f});
                //sleep_for(seconds(0.1)); // give some time to let it sink in
                // START OFFBOARD MODE
                offboard_result = offboard->start();
            }

            else
            {
                isOffboard = true;
                char* x_char = strtok(msg, " ");
                char* y_char = strtok(NULL, " ");
                char* z_char = strtok(NULL, " ");


                //std::cout << "x:" << x_char << std::endl;
                //std::cout << "y:" << y_char << std::endl;
                //std::cout << "z:" << z_char << std::endl;

                //float xTargetCenterInImageFrame = std::stof(x_char); // Given by detection algorithm (in Python)
                //float yTargetCenterInImageFrame = std::stof(y_char); // Given by detection algorithm (in Python)

                float x_real = std::stof(x_char);
                float y_real = std::stof(y_char);
                float z_real = std::stof(z_char);

                if(detected)  // to be DELETED
                {

                    //std::cout << "Target Detected:" << std::endl;
                    //psi = telemetry->attitude_euler_angle().yaw_deg;
                    //std::cout << "Yaw angle: " << psi << "s\n";

                    if (!net)
                    {
                        //std::cout << "No net, avoid" << std::endl;
                        n = x_real;
                        e = -y_real*1.0f;
                        d = -z_real*1.0f;
                        offboard->set_velocity_body({n, e, d, 0.0f});
                        auto actual_time = std::chrono::system_clock::now();
                        std::chrono::duration<double> elapsed_seconds = actual_time-start_time;
                        std::cout << "\n elapsed time: " << elapsed_seconds.count() << "s\n";
                        //time_t time (time_t* timer);
                        //current_time= time(NULL);
                        //std::cout << std::fixed;
                        //std::cout << std::setprecision(18) << current_time << " seconds\n";
                    }
                }
            }
        }
        sleep_for(seconds(1));
        //delete[] msg;
    }

    //Stop offboard mode
    offboard_result = offboard->stop();
    if (offboard_result != Offboard::Result::SUCCESS) {
            std::cerr << "Offboard::stop() failed: "
            << Offboard::result_str(offboard_result) << std::endl;
        }


    //----------------------------------------------------------------------------------//
    //----------------------------------------------------------------------------------//

    //--------------------------------<Land and disarm>---------------------------------//

    std::cout << "Landing..." << std::endl;
    const Action::Result land_result = action->land();
    if (land_result != Action::Result::SUCCESS) {
        std::cout << ERROR_CONSOLE_TEXT << "Land failed:" << Action::result_str(land_result)
                  << NORMAL_CONSOLE_TEXT << std::endl;
        return 1;
    }

    // Check if vehicle is still in air
    while (telemetry->in_air()) {
        std::cout << "Vehicle is landing..." << std::endl;
        sleep_for(seconds(1));
    }
    std::cout << "Landed!" << std::endl;

    // We are relying on auto-disarming but let's keep watching the telemetry for a bit longer.
    sleep_for(seconds(3));
    std::cout << "Finished..." << std::endl;

    delete server;

    return EXIT_SUCCESS;
}
