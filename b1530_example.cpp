#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <visa.h> //optional
#include "wgfmu.h"
#include <cstdlib>
#include <chrono>
#include <thread>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <string>

const double VOLTAGE_SAMPLING_RESOLUTION = 10e-3;
const double AVERAGING_TIME = 1e-6;

const double MAX_SET_VOLTAGE = 0.5;
const double MIN_RESET_VOLTAGE = -1.2;

const int CHANNEL1 = 101;
const int CHANNEL2 = 102;

const char* INSTRUMENT = "b1500gpib";

void measureRamps(int nCycles, double speeds[], int speedsLength, const char* basePath, long int sleep_time) {

    std::time_t t = std::time(nullptr);
    std::string datetime(100,0);
    datetime.resize(std::strftime(&datetime[0], datetime.size(), 
        "%a_%d_%b_%Y__%H_%M_%S", std::localtime(&t)));

    auto created_new_directory
        = std::filesystem::create_directory("meas");

    char meas_dir[200];
    sprintf(meas_dir, "./meas/cycles_%s", datetime.c_str());

    //printf("Meas dir: %s", meas_dir);

    auto created_meas_dir = std::filesystem::create_directory(meas_dir);
    if (not created_meas_dir) {
        printf("\nERROR: Could not create directory %s. Now exiting.\n", meas_dir);
        exit(1);
    }  

    for (int i = 0; i < speedsLength; i++) {
        printf("\n\n---- Measuring ramp %g V/s || %d/%d ----\n", speeds[i], i+1, speedsLength);

        double halfSetTime = abs(MAX_SET_VOLTAGE/speeds[i]);
        double halfResetTime = abs(MIN_RESET_VOLTAGE/speeds[i]);
        
        // Añadimos un cuarto de ciclo para evitar problemas con que haya mas tiempo de sampling que de 
        // waveform
        double totalSetTime = 2*halfSetTime ;
        double totalResetTime = 2*halfResetTime;

        long int setMeasurementPoints = floor(2*abs(MAX_SET_VOLTAGE) / VOLTAGE_SAMPLING_RESOLUTION);
        long int resetMeasurementPoints = floor(2*abs(MIN_RESET_VOLTAGE) / VOLTAGE_SAMPLING_RESOLUTION); // Floor -> Explicit better than implicit

        if (setMeasurementPoints > 4e6) {
            printf("Error: Too many set points (%d), lower the number of set measurement points.\n", setMeasurementPoints);
            return;
        }

        if (resetMeasurementPoints > 4e6) {
            printf("Error: Too many reset points (%d), lower the number of reset measurement points.\n", resetMeasurementPoints);
            return;
        }

        double setSamplingTime = (totalSetTime) / ((double)setMeasurementPoints);
        double resetSamplingTime = (totalResetTime) / ((double)resetMeasurementPoints);

        long int setSamples = floor((totalSetTime) / setSamplingTime);
        long int resetSamples = floor((totalResetTime) / resetSamplingTime);

        char cycle_dir[200];

        sprintf(cycle_dir, "%s/ramp_%g_V_per_second", meas_dir, speeds[i]);

        //printf("Meas dir: %s", meas_dir);

        auto created_new_directory = std::filesystem::create_directory(cycle_dir);
        if (not created_new_directory) {
            printf("\nERROR: Could not create directory %s. Now exiting.\n", cycle_dir);
            exit(1);
        }  


        for (int j = 0; j < nCycles; j++) {
            printf("\t                 \r");
            printf("\t %d/%d || %g V/s               \r", j+1, nCycles, speeds[i]);

            // RESET
            {
                WGFMU_clear(); //23
                WGFMU_createPattern("v1", 0);
                
                WGFMU_addVector("v1", halfResetTime, MIN_RESET_VOLTAGE);
                WGFMU_addVector("v1", halfResetTime, 0);
                WGFMU_addVector("v1", halfResetTime/2, 0);

                WGFMU_setMeasureEvent("v1", "evt", 0, resetSamples, resetSamplingTime, AVERAGING_TIME, WGFMU_MEASURE_EVENT_DATA_AVERAGED);
                WGFMU_addSequence(CHANNEL1, "v1", 1);

                WGFMU_createPattern("v2", 0);
                WGFMU_setVector("v2", totalResetTime, 0);
                WGFMU_setMeasureEvent("v2", "evt_curr", 0, resetSamples, resetSamplingTime, AVERAGING_TIME, WGFMU_MEASURE_EVENT_DATA_AVERAGED);
                WGFMU_addSequence(CHANNEL2, "v2", 1);

                // Online
                WGFMU_openSession(INSTRUMENT); // 35
                WGFMU_initialize();
                WGFMU_setOperationMode(CHANNEL1, WGFMU_OPERATION_MODE_FASTIV);
                WGFMU_setOperationMode(CHANNEL2, WGFMU_OPERATION_MODE_FASTIV);
                WGFMU_setMeasureMode(CHANNEL2, WGFMU_MEASURE_MODE_CURRENT);
                WGFMU_connect(CHANNEL1); // 40
                WGFMU_connect(CHANNEL2);
                WGFMU_execute();
                printf("\t\t\t Executing...         \r");
                WGFMU_waitUntilCompleted();
                printf("\t\t\t Saving...            \r");


                char path[120];
                sprintf(path, "%s/Cycle_R%d.txt", cycle_dir, j+1);

                FILE* fp = fopen(path, "w+");

                if (fp == 0) {
                    printf("\tCould not create file %s!\n", path);
                    exit(1);
                }

                int measuredSize, totalSize;

                WGFMU_getMeasureValueSize(CHANNEL2, &measuredSize, &totalSize);
                for (int j = 0; j < resetSamples; j++)
                {
                    /*if (j >= samples) {
                        break;
                    }*/
                    double time, value, voltage;
                    WGFMU_getMeasureValue(CHANNEL2, j, &time, &value);
                    WGFMU_getInterpolatedForceValue(CHANNEL1, time, &voltage);
                    fprintf(fp, "%.9lf %.9lf %.9lf\n", voltage, abs(value), time);
                }

                fclose(fp);
                WGFMU_closeSession();
            }

            // SET
            {
                WGFMU_clear(); //23
                WGFMU_createPattern("v1", 0);
                
                WGFMU_addVector("v1", halfSetTime, MAX_SET_VOLTAGE);
                WGFMU_addVector("v1", halfSetTime, 0);
                WGFMU_addVector("v1", halfSetTime/2, 0);

                WGFMU_setMeasureEvent("v1", "evt", 0, setSamples, setSamplingTime, AVERAGING_TIME, WGFMU_MEASURE_EVENT_DATA_AVERAGED);
                WGFMU_addSequence(CHANNEL1, "v1", 1);

                WGFMU_createPattern("v2", 0);
                WGFMU_setVector("v2", totalSetTime, 0);
                WGFMU_setMeasureEvent("v2", "evt_curr", 0, setSamples, setSamplingTime, AVERAGING_TIME, WGFMU_MEASURE_EVENT_DATA_AVERAGED);
                WGFMU_addSequence(CHANNEL2, "v2", 1);

                // Online
                WGFMU_openSession(INSTRUMENT); // 35
                WGFMU_initialize();
                WGFMU_setOperationMode(CHANNEL1, WGFMU_OPERATION_MODE_FASTIV);
                WGFMU_setOperationMode(CHANNEL2, WGFMU_OPERATION_MODE_FASTIV);
                WGFMU_setMeasureMode(CHANNEL2, WGFMU_MEASURE_MODE_CURRENT);
                WGFMU_connect(CHANNEL1); // 40
                WGFMU_connect(CHANNEL2);
                WGFMU_execute();
                printf("\t\t\t Executing...         \r");
                WGFMU_waitUntilCompleted();
                printf("\t\t\t Saving...            \r");


                char path[120];
                sprintf(path, "%s/Cycle_S%d.txt", cycle_dir, j+1);

                FILE* fp = fopen(path, "w+");

                if (fp == 0) {
                    printf("\tCould not create file %s!\n", path);
                    exit(1);
                }

                int measuredSize, totalSize;

                WGFMU_getMeasureValueSize(CHANNEL2, &measuredSize, &totalSize);
                for (int j = 0; j < setSamples; j++)
                {
                    /*if (j >= samples) {
                        break;
                    }*/
                    double time, value, voltage;
                    WGFMU_getMeasureValue(CHANNEL2, j, &time, &value);
                    WGFMU_getInterpolatedForceValue(CHANNEL1, time, &voltage);
                    fprintf(fp, "%.9lf %.9lf %.9lf\n", voltage, abs(value), time);
                }

                fclose(fp);
                
                //printf("path: %s", path);
                // Write results

                //WGFMU_initialize();
                WGFMU_closeSession();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        }
    }
}

int main() // 1
{
    double speeds[] = {
        // 0.1,
        1,
        // 100,
        // 500,
        // 1000,
    };
    int speedsLength = sizeof(speeds)/sizeof(double);
    int nCyclesPerRamp = 5;
    const char* basePath = "C:/Users/Usuario/Desktop/WGFMU_Measurements";

    measureRamps(nCyclesPerRamp, speeds, speedsLength, basePath, 1000 /* 3 minutos entre medidas */);
    

    // printf("time: %s", datetime.c_str());
}
