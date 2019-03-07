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
#include <errno.h>
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
    static void remapLogFile(const int          fileDescriptor, 
                             _RecordType        *&mapStartPtr,
                             _RecordType        *&readPtr,
                             _RecordType        *&writePtr,
                             _RecordType        *&mapEndPtr,
                             unsigned long long  &oldMapLength,
                             unsigned long long  &fileLength,
                             unsigned long long   toAppendLength) {

        // unmap the section of the file already written to and read from, but leave the portion not read yet
        unsigned long long readMapUnmapLength = (unsigned long long)readPtr - (unsigned long long)mapStartPtr;
        unsigned long long pageAlignedReadMapUnmapLength = (readMapUnmapLength / 4096) * 4096;
        unsigned long long mapPtrDelta = readMapUnmapLength - pageAlignedReadMapUnmapLength;    // difference (due to page alignment) between map start and first unread object -- otherwise they would be equal
        if (pageAlignedReadMapUnmapLength > 0) {
            if (munmap(mapStartPtr, pageAlignedReadMapUnmapLength) != 0) {
                throw std::runtime_error("Could not unmmap old map. Error: "+ string(strerror(errno)));
            }
            mapStartPtr = (_RecordType *) (((char *)mapStartPtr) + pageAlignedReadMapUnmapLength);
        }
        unsigned long long toReadLength = oldMapLength-readMapUnmapLength;
        unsigned long long newMapLength = toReadLength + toAppendLength + mapPtrDelta;

        // prepare the file for new section, unifying to read addresses & new section (with a possible relocation)
        ftruncate(fileDescriptor, fileLength + toAppendLength);
        //mapStartPtr = (_RecordType *) mmap(0, newMapLength, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NONBLOCK | MAP_NORESERVE, fileDescriptor, fileLength-toReadLength);
        mapStartPtr  = (_RecordType *) mremap(mapStartPtr, toReadLength, newMapLength, MREMAP_MAYMOVE);
        if (mapStartPtr == MAP_FAILED) {
            cerr << "\tfileDescriptor = " + to_string(fileDescriptor) << endl;
            cerr << "\tmapStartPtr    = " + to_string((unsigned long long)mapStartPtr) << endl;
            cerr << "\ttoReadLength   = " + to_string(toReadLength) << endl;
            cerr << "\tnewMapLength   = " + to_string(newMapLength) << endl;
            cerr << "\toffset         = " + to_string(fileLength-toReadLength) << endl;
            string fileName = "<name not available>";
            throw std::runtime_error("Could not mmap new section of file '" + string(fileName) + "'. Error: "+ string(strerror(errno)));
        }

        readPtr     = (_RecordType *) (((char *)mapStartPtr) + mapPtrDelta);
        writePtr    = (_RecordType *) (((char *)readPtr)     + toReadLength);
        mapEndPtr   = (_RecordType *) (((char *)mapStartPtr) + newMapLength);

        // returns
        oldMapLength = newMapLength;
        fileLength  += toAppendLength;
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
    unsigned toWriteIndex  = 0;
    unsigned logAreaLength = targetBuckets;
    output(" OK -- at address "+to_string((unsigned long)logArea)+"\n");

    LogBucket          *readPtr      = logArea;
    LogBucket          *writePtr     = logArea;
    LogBucket          *mapEndPtr    = &logArea[logAreaLength];
    unsigned long long  oldMapLength = targetBytes;
    unsigned long long  fileLength   = targetBytes;

    unsigned long  writeCount = 0;
    unsigned long  readCount  = 0;

    unsigned long long startTimestampNS = TimeMeasurements::getMonotonicRealTimeNS();

    // write some entries without a simultaneous read
    for (unsigned i=0; i<10; i++) {
        writePtr->id        = writeCount++;
        writePtr->timestamp = TimeMeasurements::getMonotonicRealTimeNS();
        writePtr++;
    }

    unsigned long long currentDelta   = 0;
    unsigned long long globalMaxDelta = 0;

    LogBucket *oldReadPtr = readPtr;
    for (unsigned growthId=1024; growthId>=1; growthId--) {

        unsigned long long maxDelta     = 0;

        while (writePtr != mapEndPtr) {
            // write
            writePtr->id        = writeCount++;
            writePtr->timestamp = TimeMeasurements::getMonotonicRealTimeNS();
            writePtr++;
            // read
            readPtr++;
            currentDelta = readPtr->timestamp - oldReadPtr->timestamp;
            if (currentDelta > maxDelta) {
                maxDelta = currentDelta;
                if (maxDelta > globalMaxDelta) {
                    globalMaxDelta = maxDelta;
                }
            }
            readCount++;
            oldReadPtr++;
        }

        output(to_string(maxDelta)+"ns\t");

        if (growthId > 1) {
            RealTimeLogger::remapLogFile<LogBucket>(fileDescriptor, logArea, readPtr, writePtr, mapEndPtr, oldMapLength, fileLength, targetBytes);
            oldReadPtr = readPtr;
        }
    }

    // read remaining entries
    readPtr++;
    while (readPtr != mapEndPtr) {
        // read
        currentDelta = readPtr->timestamp - oldReadPtr->timestamp;
        if (currentDelta > globalMaxDelta) {
            globalMaxDelta = currentDelta;
        }
        readCount++;
        oldReadPtr++;
        readPtr++;
    }

    unsigned long long elapsedTimestampNS = TimeMeasurements::getMonotonicRealTimeNS() - startTimestampNS;


    output("\nFinal worst: "+to_string(globalMaxDelta)+"ns\n");
    output("Counts: write("+to_string(writeCount)+"); read("+to_string(readCount)+")\n");
    output("Average time: "+to_string(((double)elapsedTimestampNS)/((double)writeCount)));

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
