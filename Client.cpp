#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/timer/timer.hpp>
#include <boost/chrono.hpp>
#include <boost/asio/connect.hpp>
#include <boost/system/system_error.hpp>
#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/thread.hpp>
// #include <boost/date_time/posix_time/posix_time.hpp>
// #include <boost/date_time/posix_time/posix_time_io.hpp>
#include <ctime>
#include <cstdio>
#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <time.h>
#include <string>
#include <cstdlib>

/// flags used in the 'parallelOperationTest'
/// example function
bool PM_THREAD_IS_RUNNING;
bool TL_THREAD_IS_RUNNING;
bool POST_MORTEM_STARTED;

/// arguments for the TCP connection function;
/// CONTROL_SOCKET uses port 3893
/// POST_MORTEM_SOCKET uses port 3894
const int CONTROL_SOCKET = 0;
const int POST_MORTEM_SOCKET = 1;

/// expected response from the function/procedures
/// which indicates the successful completion
const std::string RESPONSE_OK = "0";

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;
using boost::lambda::bind;
using boost::lambda::var;

/// vertical (i.e. voltage) range on the input channels of the device;
/// values from 3 to 10 (+-100 mV ... +- 20 V, respectively);
/// value -1 means that the channel is disabled in POST MORTEM
enum VERTICAL_RANGE
{
    RANGE_100_MV = 3, RANGE_200_MV, RANGE_500_MV,
    RANGE_1_V, RANGE_2_V, RANGE_5_V, RANGE_10_V, RANGE_20_V,
    DISABLE_CHANNEL = -1
};

/// Time Loss device ID == 0;
/// Post Mortem device ID == 1;
/// these ID values are used in 'postMortemViaTimeLossDeviceTest'
/// example function, which demonstrates how to swap
/// the functionality of the two devices, in order to
/// be able to read out (using a regular POST MORTEM operation)
/// the raw data from the input channels on the Time Loss device.
enum DEVICE_ID
{
    TIME_LOSS_DEVICE, POST_MORTEM_DEVICE
};

struct TimeLossSettings
{
    int numberOfIterations;
    double threshold;
    bool saveToFile;
    bool printSomeData;
};

struct PostMortemSettings
{
    double delay; // [number of samples]
    // positive delay => acquisition starts after the specified number of samples AFTER trigger
    // negative delay => the specified number of samples are acquired BEFORE trigger

    VERTICAL_RANGE range_A; // 3..10 ; -1 means "disable channel"
    VERTICAL_RANGE range_B; // 3..10 ; -1 means "disable channel"
    VERTICAL_RANGE range_C; // 3..10 ; -1 means "disable channel"
    VERTICAL_RANGE range_D; // 3..10 ; -1 means "disable channel"
    std::string triggerChannel; // A | B | C | D | EXT
    std::string triggerDirection; // RISING | FALLING | RISE_FALL
    int numberOfSamples; // -1 means maximum possible number of samples

    double samplingPeriod; // [s]
    // -1 means the minimum possible sampling period
    //
    // for 1 channel, minimum possible period is 200 ps
    // for 2 channels (only A+C or B+D), minimum possible period is 400 ps
    // for 3 or 4 channels, minimum possible period is 800 ps

    short triggerThreshold; // [mV], from 1 to 1000
    bool saveToFile;
    bool printSomeData;
};

class TCPClient
{
public:

    TCPClient(boost::asio::io_service& io_service)
        : stopped_(false),
        socket_(io_service), socket_2(io_service), deadline_(io_service)
    {}

    ~TCPClient()
    {
        stop();
    }

    void start(tcp::resolver::iterator endpoint_iter, int SOCKET_NUMBER)
    {
        start_connect(endpoint_iter, SOCKET_NUMBER);
    }

    void stop()
    {
        stopped_ = true;
        socket_.close();
        socket_2.close();
    }

    void send(std::string msg)
    {
        std::cout << "\n\n ..  SENDING : " << msg << "\n" << std::endl;
        boost::asio::write(socket_, boost::asio::buffer(msg));
    }

    void blocking_read(std::string token)
    {
        boost::asio::read_until(socket_, input_buffer_, '\n');

        if(!(isToken(token)))
        {
            std::cout << "\t ---> ERROR --- the client has not received the expected response: "
                      << token << std::endl;
            exit(1);
        }
    }

    std::string blocking_read(int number)
    {
        std::string result;

        for(int i = 0; i < number; i++)
        {
            boost::asio::read_until(socket_, input_buffer_, '\n');
            result = parseInputBuffer();
        }

        return result;
    }

    std::string blocking_read_scope(int number)
    {
        std::string result;

        for(int i = 0; i < number; i++)
        {
            boost::asio::read_until(socket_2, input_buffer_2, '\n');
            result = parseInputBufferScope();
        }

        return result;
    }

    int blocking_read_size()
    {
        std::cout << "blocking_read_size(): started" << std::endl;

        int result = 0;
        int readBytes = boost::asio::read_until(socket_, input_buffer_, '\n');
        std::cout << "blocking_read_size(): read " << readBytes << " bytes" << std::endl;
        result = parseSize();
        return result;
    }

    int blocking_read_scope_size()
    {
        std::cout << "blocking_read_scope_size(): started" << std::endl;

        int result = 0;
        int readBytes = boost::asio::read_until(socket_2, input_buffer_2, '\n');
        std::cout << "blocking_read_scope_size(): read " << readBytes << " bytes" << std::endl;
        result = parseScopeSize();
        return result;
    }

    void blocking_read_timeloss_data(int size, bool print, bool save)
    {
        std::cout << "blocking_read_timeloss_data: allocating memory for the buffer... size " << size << " bytes." << std::endl;
        timeLossData = new std::vector<int32_t>(size/4);

        std::cout << "blocking_read_timeloss_data: transferring the data..." << std::endl;
        boost::asio::read(socket_, boost::asio::buffer(*timeLossData, size));

        parseTimelossData(timeLossData, size/4, print, save);

        std::cout << "blocking_read_timeloss_data: deleting the buffer...\n\n" << std::endl;
        delete timeLossData;
    }

    void blocking_read_scope_data(int size, int num_of_blocks, bool printSomeData, bool save)
    {
        timeval start_time;
        timeval end_time;

        std::cout << "blocking_read_scope_data: num_of_blocks == " << num_of_blocks << std::endl;

        long totalTimePerChannel = 0;

        for(int i = 0; i < num_of_blocks; i++)
        {
            std::cout << "blocking_read_scope_data: reading block # " << i << std::endl;

            std::cout << "blocking_read_scope_data: allocating memory for the buffer... size " << size << " bytes." << std::endl;
            scopeData = new std::vector<int16_t>(size/2);

            gettimeofday(&start_time, 0);
            long start_time_ms = start_time.tv_sec * 1000 + start_time.tv_usec / 1000;

            std::cout << "blocking_read_scope_data: transferring the data..." << std::endl;
            boost::asio::read(socket_2, boost::asio::buffer(*scopeData));
            //socket_.read_some(boost::asio::buffer(*scopeData));

            gettimeofday(&end_time, 0);
            long end_time_ms = end_time.tv_sec * 1000 + end_time.tv_usec / 1000;

            totalTimePerChannel += (end_time_ms - start_time_ms);
            std::cout << "blocking_read_scope_data: \t scope data transfer time: " << (end_time_ms - start_time_ms) << " ms" << std::endl;

            parseScopeData(scopeData, size/2, printSomeData, save);

            std::cout << "blocking_read_scope_data: deleting the buffer... \n\n" << std::endl;

            scopeData->clear();
        }

        std::cout << "blocking_read_scope_data: total time per channel data transfer == " << totalTimePerChannel << " ms" << std::endl;

        delete scopeData;
    }


private:

    std::string parseInputBuffer()
    {
        std::string line;
        std::istream is(&input_buffer_);
        std::getline(is, line);

        std::cout << "Received: " << line << "\n";

        return line;
    }

    std::string parseInputBufferScope()
    {
        std::string line;
        std::istream is(&input_buffer_2);
        std::getline(is, line);

        std::cout << "Received scope message: " << line << "\n";

        return line;
    }

    int parseSize()
    {
        std::string line;
        std::istream is(&input_buffer_);
        std::getline(is, line);

        std::cout << "Received size: " << line << "\n";

        return boost::lexical_cast<int>(line);
    }

    int parseScopeSize()
    {
        std::string line;
        std::istream is(&input_buffer_2);
        std::getline(is, line);

        std::cout << "Received scope size: " << line << "\n";

        return boost::lexical_cast<int>(line);
    }

    void parseTimelossData(std::vector<int32_t> * data, int sz, bool print, bool save)
    {
        std::cout << "Parsing time loss data, histogram size: " << sz << " bins, "
                  << "i.e. " << (sz*1.6) << " ns."
                  << "save: " << save << std::endl;

        if(save)
            saveHistogramToFile(data);

        if(print)
        {
            std::cout << std::endl;
            std::cout << "HISTOGRAM : ";

            int printTo = (sz > 20 ) ? 20 : sz;

            for(int j = 0; j < printTo; j++)
                std::cout << j*1.6 << " , " << data->at(j) << std::endl;
            std::cout << std::endl;
        }
    }

    void parseScopeData(std::vector<int16_t> * data, int sz, bool print, bool save)
    {
        std::cout << "Parsing scope data, size: " << sz << " samples. \n";

        if(save)
            saveRawDataToFile(data);

        if(print)
        {
            std::cout << std::endl;
            std::cout << "DATA: " << std::endl;

            int printTo = 5;

            for(int j = 0; j < printTo; j++)
                std::cout << j << " , " << data->at(j) << std::endl;

            std::cout << "..." << std::endl;

            for(int j = (sz - 5); j < sz; j++)
                std::cout << j << " , " << data->at(j) << std::endl;
        }
        std::cout << std::endl;

    }

    bool isToken(std::string token)
    {
        bool result = false;

        std::string line;
        std::istream is(&input_buffer_);
        std::getline(is, line);

        std::cout << "Received: " << line << "\n";

        if (line.find(token) != std::string::npos)
        {
            result = true;
        }

        return result;
    }

    void start_connect(tcp::resolver::iterator endpoint_iter, int SOCKET_NUMBER)
    {
        if(SOCKET_NUMBER == 0)
        {
            int numberOfAttempt = 0;
            bool success = false;

            if (endpoint_iter != tcp::resolver::iterator())
            {
                std::cout << "CONTROL_SOCKET: Trying " << endpoint_iter->endpoint() << "...\n";
                boost::system::error_code ec;

                while(numberOfAttempt < 5)
                {
                    socket_.connect(endpoint_iter->endpoint(), ec);
                    std::cout << "CONTROL_SOCKET: status ++ " << ec.message() << "\n";

                    if(ec.message().compare("Success") != 0)
                    {
                        std::cout << "On the attempt .. " << numberOfAttempt++ << " / 5  :  \n";
                        std::cout << "Error during connection to CONTROL_SOCKET, port 3893.\n";
                        sleep(1);
                    }
                    else
                    {
                        success = true;
                        break;
                    }

                }

                if(!success)
                {
                    stop();
                    exit(1);
                }
            }
            else
            {
                stop();
            }
        }
        else
        {
            int numberOfAttempt = 0;
            bool success = false;

            if (endpoint_iter != tcp::resolver::iterator())
            {
                std::cout << "POST_MORTEM_SOCKET: Trying " << endpoint_iter->endpoint() << "...\n";
                boost::system::error_code ec;

                while(numberOfAttempt < 5)
                {
                    socket_2.connect(endpoint_iter->endpoint(), ec);
                    std::cout << "POST_MORTEM_SOCKET: status ++ " << ec.message() << "\n";

                    if(ec.message().compare("Success") != 0)
                    {
                        std::cout << "On the attempt .. " << numberOfAttempt++ << " / 5  :  \n";
                        std::cout << "Error during connection to POST_MORTEM_SOCKET, port 3894.\n";
                        sleep(1);
                    }
                    else
                    {
                        success = true;
                        break;
                    }
                }

                if(!success)
                {
                    stop();
                    exit(1);
                }
            }
            else
            {
                stop();
            }
        }
    }
    
    std::string get_current_time() {
    struct tm timestamp;
    time_t t = time(NULL);
    gmtime_r(&t, &timestamp);

    char buffer[21];
    snprintf(buffer, sizeof(buffer), "%d%02d%02d%02d%02d%02d",
        timestamp.tm_year + 1900,
        timestamp.tm_mon + 1,
        timestamp.tm_mday,
        timestamp.tm_hour,
        timestamp.tm_min,
        timestamp.tm_sec);

    return std::string(buffer);
}

    void saveHistogramToFile(std::vector<int32_t> * data)
    {
        std::fstream myfile;
        using namespace boost::posix_time;
        static int histogramCounter = 0;
        
        std::stringstream ss;

        
        ss <<get_current_time()<<"_TL.txt";
        // std::string name = "./TL-";
        // name += boost::lexical_cast<std::string>(histogramCounter++);
        // name += ".txt";

        myfile.open(ss.str().c_str(), std::fstream::out);

        for (int i = 0; i < data->size(); ++i)
        {
            myfile << i << " , " << data->at(i) << "\n";
        }

        myfile.close();
    }

    void saveRawDataToFile(std::vector<int16_t> * data)
    {
        std::fstream myfile;

        static int scopeCounter = 0;

        std::string name = "./PM-";
        name += boost::lexical_cast<std::string>(scopeCounter++);
        name += ".txt";

        myfile.open(name.c_str(), std::fstream::out);

        for (int i = 0; i < data->size(); ++i)
        {
            myfile << i << " , " << data->at(i) << "\n";
        }

        myfile.close();
    }


private:
    bool stopped_;
    tcp::socket socket_; // CONTROL_SOCKET
    tcp::socket socket_2; // POST_MORTEM_SOCKET
    deadline_timer deadline_;
    boost::asio::streambuf input_buffer_; // BUFFER FOR CONTROL_SOCKET
    boost::asio::streambuf input_buffer_2; // BUFFER FOR POST_MORTEM_SOCKET
    std::vector<int32_t> * timeLossData;
    std::vector<int16_t> * scopeData;
};

void establishConnection(TCPClient * c)
{
    std::cout << "establishConnection started" << std::endl;

    c->send("hello\n");
    c->blocking_read("hello"); //expecting "hello"

    c->send("version 1.0\n");
    c->blocking_read("welcome"); //expecting "welcome"

    std::cout << "establishConnection ended" << std::endl;
}

void connectDevice(TCPClient * c)
{
    std::cout << "connectDevice started" << std::endl;

    c->send("function acquireDevice\n");
    c->send("0\n");
    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    std::cout << "connectDevice ended" << std::endl;
}

void stopAcquisition(TCPClient * c)
{
    std::cout << "stopAcquisition started" << std::endl;

    c->send("procedure stopAcquisition\n");
    c->send("0\n");
    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    std::cout << "stopAcquisition ended" << std::endl;
}

void disconnectDevice(TCPClient * c)
{
    std::cout << "disconnectDevice started" << std::endl;

    c->send("procedure releaseDevice\n");
    c->send("0\n");
    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    c->send("bye\n");

    std::cout << "disconnectDevice ended" << std::endl;
}

void timeLossTest(TCPClient * c, TimeLossSettings * tlc)
{
    std::cout << "timeLossTest started" << std::endl;

    std::string thresholdStr;

    try
    {
        thresholdStr = boost::lexical_cast<std::string>(tlc->threshold);
        thresholdStr += "\n";
    }
    catch( boost::bad_lexical_cast const& )
    {
        std::cout << "timeLossTest: error -- input threshold was not valid" << std::endl;
        exit(1);
    }

    c->send("procedure setupHistogram\n");
    c->send("0\n");
    c->send(thresholdStr);
    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    for(int i = 0; i < tlc->numberOfIterations; i++)
    {
        sleep(0);

        c->send("function getHistogram\n");
        c->send("0\n");

        int size = c->blocking_read_size(); // SIZE OF HISTOGRAM
        c->blocking_read_timeloss_data(size, tlc->printSomeData, tlc->saveToFile); // TIME LOSS HISTOGRAM, int32_t VALUES
        c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK
        std::cout << "\n\n\t * * * Histogram length is " << (int)(size/4.0) << " bins, time interval " << 1.6*(size/4.0) << " ns" << std::endl;
    }

    std::cout << "timeLossTest ended" << std::endl;
}

void readTimeLossData(TCPClient * c, TimeLossSettings * tlc)
{
    std::cout << "readTimeLossData started" << std::endl;

    sleep(1);

    c->send("function getHistogram\n");
    c->send("0\n");

    int size = c->blocking_read_size(); // SIZE OF HISTOGRAM
    c->blocking_read_timeloss_data(size, tlc->printSomeData, tlc->saveToFile); // TIME LOSS HISTOGRAM, int32_t VALUES
    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    std::cout << "\n\n\t * * * Histogram length is " << (int)(size/4.0) << " bins, time interval " << 1.6*(size/4.0) << " ns" << std::endl;
    std::cout << "readTimeLossData ended" << std::endl;
}

void postMortemViaTimeLossDeviceTest(TCPClient * c, PostMortemSettings * ps)
{
    stopAcquisition(c);

    std::cout << "postMortemViaTimeLossDeviceTest started" << std::endl;

    c->send("procedure setTimelossDevice\n");
    c->send("0\n");
    c->send("1\n");
    c->blocking_read(RESPONSE_OK);

    /// PREPARATION OF THE INPUT ARGUMENTS FOR 'procedure setupPostMortem' ...
    /// ********************************************************************

    std::string THRESHOLD_str = boost::lexical_cast<std::string>(ps->triggerThreshold);
    THRESHOLD_str += "\n";

    std::string SAMPLING_PERIOD_str = boost::lexical_cast<std::string>(ps->samplingPeriod);
    SAMPLING_PERIOD_str += "\n";

    std::string NUM_OF_SAMPLES_str = boost::lexical_cast<std::string>(ps->numberOfSamples);
    NUM_OF_SAMPLES_str += "\n";

    std::string DELAY_str = boost::lexical_cast<std::string>(ps->delay);
    DELAY_str += "\n";

    std::string RANGE_A_str = boost::lexical_cast<std::string>(ps->range_A);
    RANGE_A_str += "\n";
    std::string RANGE_B_str = boost::lexical_cast<std::string>(ps->range_B);
    RANGE_B_str += "\n";
    std::string RANGE_C_str = boost::lexical_cast<std::string>(ps->range_C);
    RANGE_C_str += "\n";
    std::string RANGE_D_str = boost::lexical_cast<std::string>(ps->range_D);
    RANGE_D_str += "\n";

    std::string TRIGGER_CHANNEL_str = ps->triggerChannel + "\n";
    std::string TRIGGER_DIRECTION_str = ps->triggerDirection + "\n";

    int numberOfChannels = 0;

    if(ps->range_A > 0) numberOfChannels++;
    if(ps->range_B > 0) numberOfChannels++;
    if(ps->range_C > 0) numberOfChannels++;
    if(ps->range_D > 0) numberOfChannels++;
    /// ********************************************************************

    ///  POSTMORTEM SETUP

    c->send("procedure setupPostMortem\n");
    c->send("0\n"); //device
    c->send(DELAY_str); // delay
    c->send(RANGE_A_str);// range A
    c->send(RANGE_B_str);// range B
    c->send(RANGE_C_str);// range C
    c->send(RANGE_D_str);// range D
    c->send(TRIGGER_CHANNEL_str);// trigger channel: A | B | C | D | EXT
    c->send(THRESHOLD_str); // trigger threshold: mV // EXT trigger range 0..1000 mV
    c->send(TRIGGER_DIRECTION_str); // trigger direction: RISING | FALLING | RISE_FALL
    c->send(NUM_OF_SAMPLES_str);
    c->send(SAMPLING_PERIOD_str);

    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    /// RECEIVING THE DATA (WORKFLOW IS BLOCKED UNTIL WE RECEIVE THE DATA)
    /// NB: THE RESPONSE OF THE 'function getPostMortemData' WILL BE SENT OVER SOCKET 3894

    c->send("function getPostMortemData\n");
    c->send("0\n");

    c->blocking_read_scope(1); // response of 'getPostMortemData', expecting "0"

    int size = 1;
    int num_of_blocks = 1;

    size = c->blocking_read_scope_size(); // SIZE OF DATA BUFFER OF A CHANNEL
    num_of_blocks = c->blocking_read_scope_size(); // NUMBER OF BLOCKS IN THE DATA BUFFER

    size /= num_of_blocks;

    for(int k = 0; k < numberOfChannels; k++)
        c->blocking_read_scope_data(size, num_of_blocks, ps->printSomeData, ps->saveToFile); // CHANNEL DATA

    stopAcquisition(c);

    c->send("procedure setTimelossDevice\n");
    c->send("0\n");
    c->send("0\n");
    c->blocking_read(RESPONSE_OK);

    std::cout << "postMortemViaTimeLossDeviceTest ended" << std::endl;
}

void postMortemTest(TCPClient * c, PostMortemSettings * ps)
{
    std::cout << "postMortemTest started" << std::endl;

    /// PREPARATION OF THE INPUT ARGUMENTS FOR 'procedure setupPostMortem' ...
    /// ********************************************************************

    std::string THRESHOLD_str = boost::lexical_cast<std::string>(ps->triggerThreshold);
    THRESHOLD_str += "\n";

    std::string SAMPLING_PERIOD_str = boost::lexical_cast<std::string>(ps->samplingPeriod);
    SAMPLING_PERIOD_str += "\n";

    std::string NUM_OF_SAMPLES_str = boost::lexical_cast<std::string>(ps->numberOfSamples);
    NUM_OF_SAMPLES_str += "\n";

    std::string DELAY_str = boost::lexical_cast<std::string>(ps->delay);
    DELAY_str += "\n";

    std::string RANGE_A_str = boost::lexical_cast<std::string>(ps->range_A);
    RANGE_A_str += "\n";
    std::string RANGE_B_str = boost::lexical_cast<std::string>(ps->range_B);
    RANGE_B_str += "\n";
    std::string RANGE_C_str = boost::lexical_cast<std::string>(ps->range_C);
    RANGE_C_str += "\n";
    std::string RANGE_D_str = boost::lexical_cast<std::string>(ps->range_D);
    RANGE_D_str += "\n";

    std::string TRIGGER_CHANNEL_str = ps->triggerChannel + "\n";
    std::string TRIGGER_DIRECTION_str = ps->triggerDirection + "\n";

    int numberOfChannels = 0;

    if(ps->range_A > 0) numberOfChannels++;
    if(ps->range_B > 0) numberOfChannels++;
    if(ps->range_C > 0) numberOfChannels++;
    if(ps->range_D > 0) numberOfChannels++;
    /// ********************************************************************

    ///  POSTMORTEM SETUP

    c->send("procedure setupPostMortem\n");
    c->send("0\n"); //device
    c->send(DELAY_str); // delay
    c->send(RANGE_A_str);// range A
    c->send(RANGE_B_str);// range B
    c->send(RANGE_C_str);// range C
    c->send(RANGE_D_str);// range D
    c->send(TRIGGER_CHANNEL_str);// trigger channel: A | B | C | D | EXT
    c->send(THRESHOLD_str); // trigger threshold: mV // EXT trigger range 0..1000 mV
    c->send(TRIGGER_DIRECTION_str); // trigger direction: RISING | FALLING | RISE_FALL
    c->send(NUM_OF_SAMPLES_str);
    c->send(SAMPLING_PERIOD_str);

    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    /// RECEIVING THE DATA (WORKFLOW IS BLOCKED UNTIL WE RECEIVE THE DATA)
    /// NB: THE RESPONSE OF THE 'function getPostMortemData' WILL BE SENT OVER SOCKET 3894

    c->send("function getPostMortemData\n");
    c->send("0\n");

    c->blocking_read_scope(1); // response of getPostMortemData, expecting "0"

    int size = 1;
    int num_of_blocks = 1;

    size = c->blocking_read_scope_size(); // SIZE OF DATA BUFFER OF A CHANNEL
    num_of_blocks = c->blocking_read_scope_size(); // NUMBER OF BLOCKS IN THE DATA BUFFER

    size /= num_of_blocks;

    for(int k = 0; k < numberOfChannels; k++)
        c->blocking_read_scope_data(size, num_of_blocks, ps->printSomeData, ps->saveToFile); // CHANNEL DATA

    std::cout << "postMortemTest ended" << std::endl;
}

void getHistogramFunction(TCPClient * c, TimeLossSettings * tlc)
{
    for(int i = 0; i < tlc->numberOfIterations; i++)
    {
        /// INTERRUPTION POINT FOR THE THREAD:
        /// EVERY ITERATION CHECKS IF THE POST MORTEM THREAD
        /// STARTED THE DATA ACQUISITION. IF YES, THE TIME LOSS THREAD IS CLOSED.
        if(POST_MORTEM_STARTED)
            break;

        sleep(1);

        c->send("function getHistogram\n");
        c->send("0\n");

        int size = c->blocking_read_size(); // size of time loss histogram
        c->blocking_read_timeloss_data(size, tlc->printSomeData, tlc->saveToFile); // time loss histogram

        std::cout << "\n\n\t * * * Histogram length is " << size/4 << ", time interval " << 1.6*(size/4) << " ns" << std::endl;

        c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK
    }

    std::cout << "getHistogramFunction : THREAD ENDED" << std::endl;

    TL_THREAD_IS_RUNNING = false;
}

void getPostMortemDataFunction(TCPClient * c, int numberOfChannels, PostMortemSettings * ps)
{
    c->blocking_read_scope(1); // status response of 'getPostMortemData', expecting RESPONSE_OK

    int size = 1;
    int num_of_blocks = 1;

    size = c->blocking_read_scope_size(); // SIZE OF DATA BUFFER OF A CHANNEL

    POST_MORTEM_STARTED = true; // DISABLING THE TIME LOSS HISTOGRAM

    num_of_blocks = c->blocking_read_scope_size(); // NUMBER OF BLOCKS IN THE DATA BUFFER

    size /= num_of_blocks;

    for(int k = 0; k < numberOfChannels; k++)
        c->blocking_read_scope_data(size, num_of_blocks, ps->printSomeData, ps->saveToFile); // CHANNEL DATA

    std::cout << "getPostMortemDataFunction : THREAD ENDED" << std::endl;

    PM_THREAD_IS_RUNNING = false;
}

void parallelOperationTest(TCPClient * c, TimeLossSettings * tlc, PostMortemSettings * ps)
{
    std::cout << "parallelOperationTest started" << std::endl;

    /// TIME LOSS SETUP

    std::string thresholdStr;

    try
    {
        thresholdStr = boost::lexical_cast<std::string>(tlc->threshold);
        thresholdStr += "\n";
    }
    catch( boost::bad_lexical_cast const& )
    {
        std::cout << "timeLossTest: error -- input threshold was not valid" << std::endl;
        exit(1);
    }

    c->send("procedure setupHistogram\n");
    c->send("0\n");
    c->send(thresholdStr);
    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    /// POST MORTEM SETUP

    /// PREPARATION OF THE INPUT ARGUMENTS FOR 'procedure setupPostMortem' ...
    /// ********************************************************************

    std::string THRESHOLD_str = boost::lexical_cast<std::string>(ps->triggerThreshold);
    THRESHOLD_str += "\n";

    std::string SAMPLING_PERIOD_str = boost::lexical_cast<std::string>(ps->samplingPeriod);
    SAMPLING_PERIOD_str += "\n";

    std::string NUM_OF_SAMPLES_str = boost::lexical_cast<std::string>(ps->numberOfSamples);
    NUM_OF_SAMPLES_str += "\n";

    std::string DELAY_str = boost::lexical_cast<std::string>(ps->delay);
    DELAY_str += "\n";

    std::string RANGE_A_str = boost::lexical_cast<std::string>(ps->range_A);
    RANGE_A_str += "\n";
    std::string RANGE_B_str = boost::lexical_cast<std::string>(ps->range_B);
    RANGE_B_str += "\n";
    std::string RANGE_C_str = boost::lexical_cast<std::string>(ps->range_C);
    RANGE_C_str += "\n";
    std::string RANGE_D_str = boost::lexical_cast<std::string>(ps->range_D);
    RANGE_D_str += "\n";

    std::string TRIGGER_CHANNEL_str = ps->triggerChannel + "\n";
    std::string TRIGGER_DIRECTION_str = ps->triggerDirection + "\n";

    int numberOfChannels = 0;

    if(ps->range_A > 0) numberOfChannels++;
    if(ps->range_B > 0) numberOfChannels++;
    if(ps->range_C > 0) numberOfChannels++;
    if(ps->range_D > 0) numberOfChannels++;
    /// ********************************************************************

    ///  POSTMORTEM SETUP

    c->send("procedure setupPostMortem\n");
    c->send("0\n"); //device
    c->send(DELAY_str); // delay
    c->send(RANGE_A_str);// range A
    c->send(RANGE_B_str);// range B
    c->send(RANGE_C_str);// range C
    c->send(RANGE_D_str);// range D
    c->send(TRIGGER_CHANNEL_str);// trigger channel: A | B | C | D | EXT
    c->send(THRESHOLD_str); // trigger threshold: mV // EXT trigger range 0..1000 mV
    c->send(TRIGGER_DIRECTION_str); // trigger direction: RISING | FALLING | RISE_FALL
    c->send(NUM_OF_SAMPLES_str);
    c->send(SAMPLING_PERIOD_str);

    c->blocking_read(RESPONSE_OK); // expecting RESPONSE_OK

    /// ****************
    /// ****************
    /// ****************
    /// ****************

    /// STARTING THE OPERATION

    PM_THREAD_IS_RUNNING = true;
    TL_THREAD_IS_RUNNING = true;
    POST_MORTEM_STARTED = false;

    /// SENDING 'getPostMortemData', i.e. arming the Post Mortem device.
    /// when a trigger occurs, it will return the data over the 'POST_MORTEM_SOCKET' socket, port 3894.

    c->send("function getPostMortemData\n");
    c->send("0\n");

    /// running getHistogram via 'CONTROL_SOCKET', port 3893
    boost::thread(getHistogramFunction, c, tlc);

    /// waiting for the response from 'getPostMortemData' via 'POST_MORTEM_SOCKET', port 3894
    boost::thread(getPostMortemDataFunction, c, numberOfChannels, ps);

    while(TL_THREAD_IS_RUNNING || PM_THREAD_IS_RUNNING)
        sleep(1);

    std::cout << "parallelOperationTest ended" << std::endl;
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 3)
        {
            std::cout << "\n Usage: Client <host> < TL | PM | BOTH >\n" << std::endl;
            std::cout << "\t <host> is the IP address of the ROSY device" << std::endl;
            std::cout << "\t TL is the Time Loss Histogram mode test" << std::endl;
            std::cout << "\t PM is the Post Mortem mode test" << std::endl;
            std::cout << "\t BOTH is the test of the parallel execution of both modes" << std::endl;
            return 1;
        }

        std::string mode = argv[2];

        /// PREPARING THE TCP CONNECTION
        boost::asio::io_service io_service;
        tcp::resolver r(io_service);
        TCPClient c(io_service);
        io_service.run();


        /// CONNECTING THE 'CONTROL_SOCKET', USING PORT 3893;
        /// ALL FUNCTIONS/PROCEDURES ARE SENT TO THE SERVER
        /// USING THIS SOCKET; THE TIME LOSS HISTOGRAM DATA
        /// ARE READ OUT THROUGH THIS SOCKET AS WELL
        c.start(r.resolve(tcp::resolver::query(argv[1], "3893")), CONTROL_SOCKET);

        /// EXCHANGING THE VERIFICATION MESSAGES WITH THE ROSY DEVICE
        establishConnection(&c);

        /// AFTER THE VERIFICATION HAS ENDED SUCCESSFULLY,
        /// CONNECTING THE 'POST_MORTEM_SOCKET', USING PORT 3894
        /// THIS SOCKET IS USED ONLY FOR THE POST MORTEM DATA TRANSFER
        c.start(r.resolve(tcp::resolver::query(argv[1], "3894")), POST_MORTEM_SOCKET);

        /// Thus, two sockets are created in the application: the first one
        /// that connects to port 3893 of the ROSY, and the second one
        /// that connects to port 3894 of the ROSY


        /// ACQUIRE THE TIME LOSS AND POST MORTEM DEVICES IN ROSY
        connectDevice(&c);



        /// ***** TIME LOSS HISTOGRAM SETTINGS *****

        TimeLossSettings * tlc = new TimeLossSettings();

        tlc->numberOfIterations = 0;

        tlc->threshold = 15; // [mV] // signal threshold

        tlc->saveToFile = true;
        tlc->printSomeData = false;

        /// ************************************

        /// ***** POST MORTEM SETTINGS *****

        PostMortemSettings * ps = new PostMortemSettings();

        ps->delay = 0; // [number of samples]
        // positive delay => acquisition starts after the specified number of samples AFTER trigger
        // negative delay => the specified number of samples are acquired BEFORE trigger

        ps->range_A = RANGE_1_V;
        ps->range_B = DISABLE_CHANNEL;
        ps->range_C = DISABLE_CHANNEL;
        ps->range_D = DISABLE_CHANNEL;

        ps->triggerChannel = "EXT"; // A | B | C | D | EXT
        ps->triggerDirection = "RISING"; // RISING | FALLING | RISE_FALL
        ps->triggerThreshold = 250; // [mV], from +1 to +1000 mV

        ps->numberOfSamples = 1E6; // minimum 100 samples, max (1E9/selected channels)
        // -1 means maximum possible number of samples

        ps->samplingPeriod = -1; // [s]
        // -1 means the minimum possible sampling period
        //
        // for 1 channel, minimum possible period is 200 ps
        // for 2 channels (only A+C or B+D), minimum possible period is 400 ps
        // for 3 or 4 channels, minimum possible period is 800 ps

        ps->saveToFile = true;
        ps->printSomeData = true;

        /// ************************************

        if(mode.compare("TL") == 0) /// TIME LOSS MODE TEST
        {
            timeLossTest(&c, tlc); // Setup the Time Loss mode, get the histogram data
            stopAcquisition(&c); // Stop the data acquisition
            readTimeLossData(&c, tlc); // Get the histogram data after the data acquisition is stopped
        }
        else if(mode.compare("PM") == 0) /// POST MORTEM MODE TEST
        {
            postMortemTest(&c, ps); // Setup the Post Mortem mode, get the data (blocks workflow until the trigger)
            stopAcquisition(&c); // Stop the data acquisition
        }
        else if(mode.compare("BOTH") == 0) /// TIME LOSS AND POST MORTEM MODES PARALLEL OPERATION
        {
            parallelOperationTest(&c, tlc, ps); // Run Time Loss mode and Post Mortem mode in parallel threads
            // the Post Mortem thread waits for the trigger
            stopAcquisition(&c);
            readTimeLossData(&c, tlc); // Get the histogram data after the data acquisition is stopped
        }

        /// *** TESTING THE POST MORTEM DATA READING FROM THE TIME LOSS DEVICE
        //postMortemViaTimeLossDeviceTest(&c, ps); // Get Post Mortem data from the Time Loss device
        //stopAcquisition(&c); // Stop the data acquisition
        /// ***


        /// RELEASE THE DEVICES IN ROSY
        disconnectDevice(&c);
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}











