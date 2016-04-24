#include <iomanip>
#include "mraa.hpp"
#include "grove.h"
#include <mraa/aio.h>
#include <iostream>
#include <unistd.h>
#include <upm/buzzer.h>
#include "buzzer.h"
#include <ldt0028.h>
#include <mma7660.h>
#include <sqlite3.h>
#include <pthread.h>
#include <time.h>
#include <ctime>
#include <sstream>
#include <string>

using namespace std;

upm::Buzzer* buzzer;
upm::LDT0028* vibrationSensor;
upm::MMA7660* accelerometer;

bool musicPlaying;

const int buzzerPin = 5;
const int diaperPailButtonPin = 7;
const float movementStdThreshold = 0.25;
const int numMovementsThreshold = 6;

/**
 * playSong(int* notes, int* durations, int num_notes)
 *
 * Play's a
 *
 * notes - an array of the note pitches
 * durations - an array of the duration of notes
 * num_notes - number of notes in the song
 */
void playSong(int* notes, int* durations, int num_notes)
{
	for(int i=0; i<num_notes; i++)
	{
		buzzer->playSound(notes[i], durations[i]);
		buzzer->stopSound();
		usleep(100000);
	}
}

//
// Songs to play!!!
//

/**
 * playTwinkleTwinkle()
 *
 * Plays "Twinkle Twinkle Little Star"
 */
void *playTwinkleTwinkle( void *ptr )
{
	// These are the notes
	int twinkle_notes[] = {DO, DO, SOL, SOL, LA, LA, SOL, FA, FA, MI, MI, RE, RE, DO};
	int twinkle_durations[] = {500000, 500000, 500000, 500000, 500000, 500000, 1000000, 500000, 500000, 500000, 500000, 500000, 500000, 1000000};
	int numNotes = 14;
    playSong(twinkle_notes, twinkle_durations, numNotes);
    musicPlaying = false;
}


float computeMean(float* values, int numValues)
{
	float mean = 0.0;

	for(int i=0; i<numValues; i++)
	{
		mean += values[i];
	}
	mean /= numValues;

	return mean;
}


float computeStd(float* values, float mean, int numValues)
{
	float stdev = 0.0;

	for(int i=0; i<numValues; i++)
	{
		float diff = values[i] - mean;
		float s = diff*diff;
		stdev += s;
	}
	stdev /= numValues;

	return stdev;
}

//
// Database events
//

const char* SLEEP_FUSSY = "FUSSY";
const char* SLEEP_AWAKE = "AWAKE";
const char* SLEEP_PUT_DOWN = "PUT DOWN";
const char* SLEEP_GET_UP = "GET UP";

static int db_callback(void* notUsed, int argc, char** argv, char** azColName)
{
	// Not sure why this is necessary, see the sqlite3 c++ reference
	return 0;
}

/**
 * recordSleep(sqlite3* db, char* type)
 *
 * Records the sleep event, assuming the time is now, to the database
 *
 * db - the database to record into
 * type - what kind of sleep event is it?
 */
int recordSleep(const char* type)
{
	int return_code;
	char* err_msg=0;

	sqlite3* db;
	// Open the database
	return_code = sqlite3_open("/home/root/nosleep.db", &db);

	if(return_code)
	{
		cout << "grrr..." << endl;
	}
	else
	{
		cout << "Database made successfully!" << endl;
	}

	// Get the current time
	time_t now = time(0);
	char* now_str = std::ctime(&now);

	// Make the SQL statement with a stringstream
	std::stringstream ss;
	ss << "INSERT INTO sleep(UNIXTIME,TEXTTIME,EVENT_TYPE) VALUES(" << now << ",'" << now_str << "','" << type << "');";
	std::string sql = ss.str();

	// And write it to the database
	return_code = sqlite3_exec(db, sql.c_str(), db_callback, 0, &err_msg);

	if( return_code != SQLITE_OK ){
		std::cout << "SQL error: " << err_msg << std::endl;
		sqlite3_free(err_msg);
	}
	else
	{
		std::cout << "Records created successfully" << std::endl;
	}
	sqlite3_close(db);

	return return_code;
}


/**
 * recordDiaper()
 *
 * Records a diaper event, assuming the time is now, to the database
 */
int recordDiaper()
{
	int return_code;
	char* err_msg=0;

	sqlite3* db;
	// Open the database
	return_code = sqlite3_open("/home/root/nosleep.db", &db);

	if(return_code)
	{
		cout << "grrr..." << endl;
	}
	else
	{
		cout << "Database made successfully!" << endl;
	}

	// Get the current time
	time_t now = time(0);
	char* now_str = std::ctime(&now);

	// Make the SQL statement with a stringstream
	std::stringstream ss;
	ss << "INSERT INTO diaper(UNIXTIME,TEXTTIME) VALUES(" << now << ",'" << now_str << "');";
	std::string sql = ss.str();

	// And write it to the database
	return_code = sqlite3_exec(db, sql.c_str(), db_callback, 0, &err_msg);

	if( return_code != SQLITE_OK ){
		std::cout << "SQL error: " << err_msg << std::endl;
		sqlite3_free(err_msg);
	}
	else
	{
		std::cout << "Records created successfully" << std::endl;
	}
	sqlite3_close(db);

	return return_code;
}

/**
 * Main code
 */
int main() {
  /* Setup your example here, code that should run once
   */

	pthread_t musicThread;
	musicPlaying = false;

	// Diaper pail button and pail state
	upm::GroveButton* pailButton = new upm::GroveButton(diaperPailButtonPin);
	bool pailOpen = false;

	// Create the buzzer and make sure it is off
	buzzer = new upm::Buzzer(buzzerPin);
	buzzer->setVolume(0.25);
	buzzer->stopSound();


	// Set up the accelerometer
	accelerometer = new upm::MMA7660(MMA7660_I2C_BUS, MMA7660_DEFAULT_I2C_ADDR);
	accelerometer->setModeStandby();
	accelerometer->setSampleRate(upm::MMA7660::AUTOSLEEP_64);
	accelerometer->setModeActive();

	// Indicate that the program is starting...
	std::cout << "Starting NoSleepTillBrooklyn" << endl;
	time_t now = time(0);
	std::cout << "Time: " << now << "  " <<  std::ctime(&now) << std::endl;

	recordSleep(SLEEP_PUT_DOWN);

	vibrationSensor = new upm::LDT0028(0);

	// Loop count
	int i=0;

	// How many times has movement occurred?
	int movementCount = 0;

	// Array of the last 5 seconds of float values
	float accelerometerValues[100];

	float ax, ay, az;

  /* Code in this loop will run repeatedly
   */
  for (;;) {
	  i++;

//	  std::cout << vibrationSensor->getSample() << " ";

	  accelerometer->getAcceleration(&ax, &ay, &az);

	  accelerometerValues[i%100] = ax*ax + ay*ay + az*az;

	  // Should we check the accelerometer?
	  if (i%100 == 0)
	  {
		  float mean = computeMean(accelerometerValues, 100);
		  float stdev = computeStd(accelerometerValues, mean, 100);

		  std::cout << mean << "  " << stdev << "  " << movementCount <<  endl;

		  if(stdev > movementStdThreshold)
		  {
			  movementCount += 1;

			  if(movementCount > numMovementsThreshold)
			  {
				  // Record that the child is fussy
				  recordSleep(SLEEP_FUSSY);

				  // Start a thread to play the music
				  if(!musicPlaying)
				  {
					  musicPlaying = true;
					  int tmp = pthread_create(&musicThread, NULL, playTwinkleTwinkle, NULL);
				  }
				  movementCount = 0;
			  }
		  }
		  else
		  {
			  movementCount = 0;
		  }
	  }

	  // Check if the pail has been opened
	  if(pailButton->value() && !pailOpen)
	  {
		  std::cout << "Diaper Pail Opened" << std::endl;
		  pailOpen = true;
		  recordDiaper();
	  }
	  // Check if the pail has been closed
	  if(!pailButton->value() && pailOpen)
	  {
		  std::cout << "Diaper Pailed Closed" << std::endl;
		  pailOpen = false;
	  }

	  // Wait a time step...
	  usleep(20000);
  }

  return 0;
}
