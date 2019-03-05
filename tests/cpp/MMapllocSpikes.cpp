#include <iostream>
#include <fstream>
#include <csignal>
#include <cstring>
#include <thread>
#include <initializer_list>
using namespace std;

#include <boost/test/unit_test.hpp>
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE MMapllocSpikes
#include <boost/test/unit_test.hpp>

#include <MutuaTestMacros.h>
#include <BetterExceptions.h>
#include <TimeMeasurements.h>
using namespace mutua::cpputils;


#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
struct RealTimeLogger {

    static void createSparseFile(long sizeBytes, char *fileName, int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) {
        int fileDes = open(fileName, O_WRONLY | O_CREAT, mode);
        if (fileDes == -1) {
            throw std::runtime_error("Could not create sparse file '" + string(fileName) + "' with " + to_string(sizeBytes) + " bytes");
        }
        ftruncate(fileDes, sizeBytes);
        close(fileDes);
    }

    /** Please note that when writting to mmaped memory, first a pagafault is issued, meaning a read will be
     *  done prior to a write. However, this do not happen if the file is sparse. */
    template <typename _RecordType>
    static _RecordType *mmapSparseFile(int &fileDescriptor, long sizeBytes, char *fileName, int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) {
        fileDescriptor = open(fileName, O_RDWR | O_CREAT, mode);
        if (fileDescriptor == -1) {
            throw std::runtime_error("Could not open sparse file '" + string(fileName) + "' with " + to_string(sizeBytes) + " bytes");
        }
        ftruncate(fileDescriptor, sizeBytes);
        _RecordType *mmapPtr = (_RecordType *) mmap(0, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NONBLOCK | MAP_NORESERVE, fileDescriptor, 0);
        //close(fileDes);
        if (mmapPtr == MAP_FAILED) {
            throw std::runtime_error("Could not mmap file '" + string(fileName));
        }
        return mmapPtr;
        // int munmap(void *addr, size_t len);
    }

    /** When a log mmaped file is almost full, this function will help growing the file.
     *  Requisites: if 'oldPtr' is given, it means part of the old mapping must be kept at an unchanged address
     *  A new 'ftruncate()' will be used to grow the file and the new mapping will have 'length' bytes
     */
    template <typename _RecordType>
    static _RecordType *remapLogFile(const int          fileDescriptor, 
                                     _RecordType      *&unmapStart,
                                     long              &unmapLength,
                                     _RecordType       *oldMapStart,
                                     _RecordType       *keepPtr,
                                     long              &oldMapLength,
                                     long              &fileLength,
                                     const long         toAppendLength) {

        // unmap the section of the file no longer used
        if (unmapLength > 0) {
            munmap(unmapStart, unmapLength);
        }

        // unmap the section of the file already written to, but not read yet
        long notReadUnmapLength = (long)keepPtr - (long)oldMapStart;
        if (notReadUnmapLength > 0) {
            munmap(oldMapStart, notReadUnmapLength);
        }

        // prepare the new section
        ftruncate(fileDescriptor, fileLength + toAppendLength);
        _RecordType *mmapPtr = (_RecordType *) mmap(0, toAppendLength, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NONBLOCK | MAP_NORESERVE, fileDescriptor, fileLength);
        if (mmapPtr == MAP_FAILED) {
            string fileName = "<name not available>";
            throw std::runtime_error("Could not mmap new section of file '" + string(fileName));
        }

        // returns
        //////////

        // set old maps to unmap on next call
        unmapStart  = keepPtr;
        unmapLength = oldMapLength-notReadUnmapLength;

        // set write maps to read maps on next call
        oldMapLength = toAppendLength;

        fileLength += toAppendLength;
        return mmapPtr;
    }

};


struct LogBucket {
    unsigned           id;
    unsigned long long timestamp;
};


struct MMapSuiteObjects {

    // test case constants
    //static constexpr unsigned xxx  = n'nnn;

    // test case data
    //static string[] yyy;

    // output messages for boost tests
    static string testOutput;


    MMapSuiteObjects() {
    	static bool firstRun = true;
    	if (firstRun) {
			cerr << endl << endl;
			cerr << "MMap Experiments:" << endl;
			cerr << "================ " << endl << endl;
			firstRun = false;
    	}
    }
    ~MMapSuiteObjects() {
    	BOOST_TEST_MESSAGE("\n" + testOutput);
    	testOutput = "";
    }

    static void output(string msg, bool toCerr) {
    	if (toCerr) cerr << msg << flush;
    	testOutput.append(msg);
    }
    static void output(string msg) {
    	output(msg, true);
    }

};
// static initializers
string MMapSuiteObjects::testOutput = "";

BOOST_FIXTURE_TEST_SUITE(MMapSuite, MMapSuiteObjects);

BOOST_AUTO_TEST_CASE(mmapLoggingTest) {

    // Determine the log file max size
    // 'targetBytes' will give multiples of 'sizeof(LogBucket)' up to 'targetMiB'
    constexpr unsigned long targetMiB     = 8;
    constexpr unsigned long targetBuckets = (targetMiB*1024*1024) / sizeof(LogBucket);
    constexpr unsigned long targetBytes   = targetBuckets * sizeof(LogBucket);

    char *logFile = "/tmp/logTesting";

    // output("Creating ~ "+to_string(targetMiB)+"MiB sparse file able to store "+to_string(targetBuckets)+" log entries...");
    // RealTimeLogger::createSparseFile(targetBytes, logFile);
    // output(" OK\n");

    output("MMapping the sparse log file...");
    int fileDescriptor;
    LogBucket *logArea = RealTimeLogger::mmapSparseFile<LogBucket>(fileDescriptor, targetBytes, logFile);
    unsigned logAreaLength = targetBuckets;
    output(" OK -- at address "+to_string((unsigned long)logArea)+"\n");

    LogBucket *unmapStart  = nullptr;
    long       unmapLength = 0;
    long      oldMapLength = 0;
    long       fileLength  = targetBytes;

    for (unsigned growthId=0; growthId<1024; growthId++) {
//        output("Writing (possibly out of order) objects: ");
        for (unsigned i=0; i<logAreaLength; i++) {
//        cerr << "writing #" << i << flush;
            logArea[i].id = i;
            logArea[i].timestamp = TimeMeasurements::getMonotonicRealTimeNS();
//        cerr << "." << endl << flush;
        }
//        output("DONE\n");

        LogBucket *toReadLogArea = logArea;
        logArea = RealTimeLogger::remapLogFile<LogBucket>(fileDescriptor, unmapStart, unmapLength, toReadLogArea, toReadLogArea, oldMapLength, fileLength, targetBytes);

//        output("Computing elapsed time of written log entries: ");
        unsigned long long currentDelta = 0;
        unsigned long long maxDelta     = 0;
        for (unsigned i=1; i<logAreaLength; i++) {
            currentDelta = toReadLogArea[i].timestamp - toReadLogArea[i-1].timestamp;
            if (currentDelta > maxDelta) {
                maxDelta = currentDelta;
//                output(".");
            }
        }
//        output("DONE -- maxDelta: "+to_string(maxDelta)+"ns\n");
        output(to_string(maxDelta)+"ns\t");
    }


    // 1) create a file with enough size -- how?
    // 2) mmap the whole stuff
    // 3) start writing pages, measuring times -- CloudXXX as a lib?
    // 4) detect file limit at each write and create a new file if necessary
    // 5) close all shits and flush
}

BOOST_AUTO_TEST_SUITE_END();


boost::unit_test::test_suite* init_unit_test_suite(int argc, char* args[]) {
	boost::unit_test::framework::master_test_suite().p_name.value = "MMapllocSpikes";
	return 0;
}
