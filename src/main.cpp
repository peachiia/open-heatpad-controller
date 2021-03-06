#include <Arduino.h>
#include <EEPROM.h>
#include <stdio.h>
//#define VERBOSE

struct profile_storage
{
	uint8_t THERMISTER_PIN = 0;
	double THERMISTER_REF_RESISTER = 9960.0;
	int ADC_MAX = 1023;

	double THERMISTER_ROOM_RESISTANCE = 10000;    	// Resistance @ Norminal  
	double THERMISTER_ROOM_TEMP = 25;				// Temp @ Norminal
	double THERMISTER_COEF_B = 2988.64;				// Beta Coefficient of thermistor
													// 2988.64 == getBetaCoef(62, 3324.33, 5.5, 20274.66)

	int DENOISE_ORDER = 1;
	double DENOISE_DIFF_LIMIT = 0.02;

	int CONTROLLER_PWM_MAX = 255;
	uint8_t CONTROLLER_PWM_PIN = 6;
	uint8_t CONTROLLER_INA_PIN = 7;
	uint8_t CONTROLLER_INB_PIN = 8;

	double kP = 500.0; 
	double kI = 0;
	double kD = 0;

	double setpointTemp = 40.0;

	bool isAutorunEnabled = false;
	bool isPlottingTaskEnabled = false;
} profile;

#pragma region TASK
	#define STATIC_TIMER_UPDATE timer = millis() + duration					// Reschedule for next calling
	#define STATIC_TIMER_INIT static unsigned long STATIC_TIMER_UPDATE		// Schedule Variable Declaration
	#define STATIC_TIMER_CHECK (millis() >= timer)							// Schedule Checking

	bool task_run();
	bool task_stop();
	bool task_isRunning();

	bool isControlTaskEnabled = false;
#pragma endregion

#pragma region THERMISTER
	#define TEMP_KELVIN_DIFF 273.15				// Constant Difference between Kelvin to Celcius

	double resistance = 10000.0;
	double noisyTemp = 25.0;
	double denoisedTemp = 25.0;
	double currentTemp = 25.0;

	double getResistance(double Rref, int ADCval);
	double toCelcius(double resistance);
	double getBetaCoef(double T1, double R1, double T2, double R2);
	double getDenoisedData(double data);
#pragma endregion

#pragma region CONTROLLER
	#define CONTROLLER_MINIMUM_START 5
	void init_Controller();
	void startController();
	void stopController();
	int getPwnFromPercentage(double percentage);
	bool setController(double percentage);
#pragma endregion

#pragma region PID
	double valP, valI, valD, valPID;

	double previousTemp = 25.0;
	double errorTemp = 0;

	double deltaTime = 0;
	double controlPWM = 0;
#pragma endregion

#pragma region TERMINAL
	#define COMMAND_MAX_LENGTH 50
	char bufferString[COMMAND_MAX_LENGTH];
	int bufferLength = 0;
	char bufferChar;

	char command[COMMAND_MAX_LENGTH];
	int commandLength = 0;
	bool flagExecRequest = false;
	bool flagTerminalResume = true;

	void init_Terminal();
#pragma endregion


void task_Temperature(long duration);
void task_PID(long duration);
void task_Controller(long duration);
void task_Plot(long duration);
void task_Terminal(long duration);
void task_Exec();

bool isMatch(char *p,  char *keyword);
void command_help();
void command_info();
void command_run();
void command_stop();
void command_default();
void command_show();
void command_load();
void command_save();
void command_set();

bool profile_save();
bool profile_load();

void setup()
{
	pinMode(LED_BUILTIN, OUTPUT);
	profile_load();
	init_Terminal();
	if (profile.isAutorunEnabled) {
		task_run();
	}
}

void loop()
{
	if (task_isRunning())
	{
		task_Temperature(10);
		task_PID(50);
		task_Controller(50);
		if (profile.isPlottingTaskEnabled) {
			task_Plot(20);
		}
	}
	task_Terminal(50);
	task_Exec();
}


void init_Temperature()
{
	resistance = 10000.0;
	noisyTemp = 25.0;
	denoisedTemp = 25.0;
	currentTemp = 25.0;
}


void task_Temperature(long duration)
{
	STATIC_TIMER_INIT;
	if (STATIC_TIMER_CHECK) {
		resistance = getResistance(profile.THERMISTER_REF_RESISTER, analogRead(profile.THERMISTER_PIN));
		noisyTemp = toCelcius(resistance);
		denoisedTemp = getDenoisedData(noisyTemp);
		currentTemp = denoisedTemp;
		STATIC_TIMER_UPDATE;
	}
}


void init_PID()
{
	valPID = 0;

	previousTemp = 25.0;
	errorTemp = 0;

	deltaTime = 0;
	controlPWM = 0;
}


void task_PID(long duration)
{
	STATIC_TIMER_INIT;
	if (STATIC_TIMER_CHECK) {
		double previousTime = millis();
		errorTemp = profile.setpointTemp - currentTemp;
		deltaTime = (millis() - previousTime) / 1000;
		previousTime = millis();

		valP = errorTemp * profile.kP;
		valI = valI + ((errorTemp * profile.kI) * deltaTime);
		valD = (previousTemp - currentTemp) * profile.kD;
		valPID = valP + valI + valD;

		STATIC_TIMER_UPDATE;
	}
}


void init_Controller()
{
	#ifdef VERBOSE
		Serial.println("Init Controller");
	#endif
	pinMode(profile.CONTROLLER_PWM_PIN, OUTPUT);
	pinMode(profile.CONTROLLER_INA_PIN, OUTPUT);
	pinMode(profile.CONTROLLER_INB_PIN, OUTPUT);

	startController();

	setController(0);
}

void startController()
{
	digitalWrite(profile.CONTROLLER_INA_PIN, HIGH);
	digitalWrite(profile.CONTROLLER_INB_PIN, LOW);
}

void stopController()
{
	digitalWrite(profile.CONTROLLER_INA_PIN, LOW);
	digitalWrite(profile.CONTROLLER_INB_PIN, LOW);

	setController(0);
}


void task_Controller(long duration)
{
	static bool flagJustStop = true;
	static double percentage;
	STATIC_TIMER_INIT;
	if (STATIC_TIMER_CHECK) {
		percentage = valPID;
		if (percentage < 0) {
			percentage = 0;
		}
		else if (percentage > 100) {
			percentage = 100;
		}
		
		if (percentage > CONTROLLER_MINIMUM_START) {
			if (flagJustStop){
				startController();
				flagJustStop = false;
			}
			setController(percentage);
		}
		else {
			stopController();
			flagJustStop = true;
		}
		
		STATIC_TIMER_UPDATE;
	}
}


void init_Plot()
{

}


void task_Plot(long duration)
{
	STATIC_TIMER_INIT;
	if (STATIC_TIMER_CHECK) {
		// Serial.print(noisyTemp);
		// Serial.print(" ");
		Serial.print(denoisedTemp);
		Serial.print(" ");
		Serial.print(profile.setpointTemp);
		// Serial.print(" // ");
		// Serial.print(valPID);

		Serial.println();
		STATIC_TIMER_UPDATE;
	}

	if (abs(currentTemp-profile.setpointTemp) < 0.5) {
		digitalWrite(LED_BUILTIN, HIGH);
	}
	else {
		digitalWrite(LED_BUILTIN, LOW);
	}
}


double getResistance(double Rref, int ADCval)
{
	// For Normal Measuring Circuit
	// return Rref / ((ADC_MAX/ (double)ADCval) - 1);

	// For Reverse Measuring Circuit
	return Rref * ((profile.ADC_MAX / ((double)ADCval)) - 1); 
}


double getDenoisedData(double data)
{
	static double buffer = data;
	static double prev = data;

	if((data - prev) > profile.DENOISE_DIFF_LIMIT) {
		prev += profile.DENOISE_DIFF_LIMIT;
		data = prev ;
	}
	else if ((prev - data) > profile.DENOISE_DIFF_LIMIT) {
		prev -= profile.DENOISE_DIFF_LIMIT;
		data = prev;
	} 
	else {
		prev = data;
	}
	buffer = (data + (buffer * (profile.DENOISE_ORDER - 1))) / profile.DENOISE_ORDER;
	return buffer;
}


double toCelcius(double resistance)
{
	// Steinhart-Hart eq. (NTC - Beta method)
	double value = 0;
	value = resistance / profile.THERMISTER_ROOM_RESISTANCE;    // (R/Ro)
	value = log(value);									// ln(R/Ro)
	value /= profile.THERMISTER_COEF_B;							// 1/B * ln(R/Ro)
	value += 1.0 / (profile.THERMISTER_ROOM_TEMP + TEMP_KELVIN_DIFF);		// + (1/To)
	value = 1.0 / value;								// Invert
	value -= TEMP_KELVIN_DIFF;							// Convert to C
	return value;
}


double getBetaCoef(double T1, double R1, double T2, double R2)
{
	// For NTC Thermistor. See "Steinhart-Hart" for more informations.
	
	// Convert T1 and T2 to Kelvin
	T1 += TEMP_KELVIN_DIFF;
	T2 += TEMP_KELVIN_DIFF;

	return log(R1/R2) / ((1/T1) - (1/T2));
}


int getPwnFromPercentage(double percentage)
{
	return (int)(profile.CONTROLLER_PWM_MAX * (percentage/100));
}

bool setController(double percentage)
{
	static int pwm = getPwnFromPercentage(percentage);
	#ifdef VERBOSE
		Serial.print("setController: ");
		Serial.print(percentage);
		Serial.print("% -> PWM : ");
		Serial.println(pwm);
	#endif
	if (pwm >= 0 && pwm <= profile.CONTROLLER_PWM_MAX) {
		analogWrite(profile.CONTROLLER_PWM_PIN, pwm);
		return true;
	}
	else {
		return false;
	}
}


void init_Terminal()
{
	Serial.begin(115200);
}


void task_Terminal(long duration)
{
	STATIC_TIMER_INIT;
	if (STATIC_TIMER_CHECK) {
		if (flagTerminalResume) {
			Serial.print(F(">> "));
			flagTerminalResume = false;
		}					
		while (Serial.available() > 0) // TODO: Watchdog Timer Required.
		{
			if (!flagExecRequest) {
				bufferChar = (char)Serial.read();
				if (bufferChar == '\n' || bufferChar== '\r') {
					if (bufferLength > 0) {
						bufferString[bufferLength] = '\0';
						memcpy(command, bufferString, bufferLength + 1);
						commandLength = bufferLength;
						bufferLength = 0;
						flagExecRequest = true;
						Serial.println();
						break;
					} 
				}
				else if (bufferChar == '\b' || bufferChar == 127) {  // backspace for windows, putty
					if (bufferLength >= 1) {
						bufferLength--;
						Serial.print(bufferChar);
					}
					else {
						bufferLength = 0;
					}
				}
				else if (bufferChar == ' ') {
					if ((bufferLength > 0) && (bufferString[bufferLength-1] != ' ')) {
						Serial.print(bufferChar);
						bufferString[bufferLength++] = bufferChar;
					}
				}
				else {
					Serial.print(bufferChar);
					bufferString[bufferLength++] = bufferChar;
				}
			}
		}
		STATIC_TIMER_UPDATE;
	}
}


void task_Exec()
{
	if (flagExecRequest) {
		#ifdef VERBOSE
			Serial.print("EXEC: ");
			Serial.println(command);
		#endif

		if (isMatch(command, "help")) {
			command_help();
		}
		else if (isMatch(command, "info")) {
			command_info();
		}
		else if (isMatch(command, "run")) {
			command_run();
		}
		else if (isMatch(command, "stop")) {
			command_stop();
		}
		else if (isMatch(command, "default")) {
			command_default();
		}
		else if (isMatch(command, "show")) {
			command_show();
		}
		else if (isMatch(command, "load")) {
			command_load();
		}
		else if (isMatch(command, "save")) {
			command_save();
		}
		else if (isMatch(command, "set")) {
			command_set();
		}
		else {
			Serial.println("Unknown Command!. Try 'help'.");
		}
		Serial.println();
		flagExecRequest = false;
		flagTerminalResume = true;
	}
}


bool isMatch(char *p, char *keyword)
{
	int i, len = strlen(keyword);
	for (i = 0; i < len; i++)
	{
		if (tolower(p[i]) != tolower(keyword[i])) {
			break;
		}
	}
	return (i == len);
}


void command_help()
{
	Serial.print( F(  "\r\n  Open Heat-Pad Controller Terminal\r\n" \
						"    help        : show help message\r\n" \
						"    info        : show info of system\r\n" \
						"    run         : start control loop\r\n" \
						"    stop        : stop control loop\r\n" \
						"    show        : show current profile\r\n" \
						"    load        : load profile from Storage\r\n" \
						"    save        : save profile to Storage\r\n" \
						"    set <k> <v> : set value to specific profile key\r\n" \
						"    default     : restore profile on RAM to DEFAULT\r\n" \
					));
}


void command_info()
{
	Serial.print(F("\r\n  System Infomation\r\n"));
	Serial.print(F("   - Status        : "));
	Serial.println(task_isRunning()? "Running":"STOP");

	Serial.print(F("   - Setpoint Temp.: "));
	Serial.println(profile.setpointTemp);

	if(task_isRunning()) {
		Serial.print(F("   - Current Temp. : "));
		Serial.println(currentTemp);
	}
}


void command_run()
{
	Serial.print(F("Task starting... "));
	if (!task_isRunning()) {
		task_run();
		Serial.print(F("DONE\r\n"));
	}
	else{
		Serial.print(F("FAIL: already running\r\n"));
	}
	command_info();
}


void command_stop()
{
	Serial.print(F("Task stopping... "));
	task_stop();
	Serial.print(F("DONE\r\n"));
	command_info();
}


bool task_run() // TODO: check current status first
{
	isControlTaskEnabled = true;
	init_Temperature();
	init_PID();
	init_Controller();
	init_Plot();
	init_Terminal();
	return true;
}


bool task_stop() // TODO: check current status first
{
	isControlTaskEnabled = false;
	setController(0);
	return true;
}


bool task_isRunning()
{
	return isControlTaskEnabled;
}


void command_default()
{
	if (task_isRunning()) {
		Serial.print(F("ERROR: Use 'stop' to stop control task first."));
	}
	else {
		Serial.print(F("Set Default to Current Profile... "));
		struct profile_storage default_profile;
		profile = default_profile;
		Serial.print(F("DONE\r\n"));

		command_show();
	}
}


void command_show()
{
	Serial.println(F("\r\nCurrent Profile")); 
	Serial.println(F("--------------------")); 
	Serial.print(F("11 - setpointTemp : ")); 
	Serial.println(profile.setpointTemp);
	Serial.print(F("12 - isAutorunEnabled : ")); // = 0;
	Serial.println(profile.isAutorunEnabled);
	Serial.print(F("13 - isPlottingTaskEnabled : ")); // = 0;
	Serial.println(profile.isPlottingTaskEnabled);
	Serial.println();

	Serial.print(F("21 - ADC_MAX : ")); // = 1023;
	Serial.println(profile.ADC_MAX);
	Serial.print(F("22 - THERMISTER_PIN : ")); //  = 0
	Serial.println(profile.THERMISTER_PIN);
	Serial.print(F("23 - THERMISTER_REF_RESISTER : ")); // = 9960.0;
	Serial.println(profile.THERMISTER_REF_RESISTER);
	Serial.print(F("24 - THERMISTER_ROOM_RESISTANCE : ")); // = 10000;    
	Serial.println(profile.THERMISTER_ROOM_RESISTANCE);
	Serial.print(F("25 - THERMISTER_ROOM_TEMP : ")); // = 25;				
	Serial.println(profile.THERMISTER_ROOM_TEMP);
	Serial.print(F("26 - THERMISTER_COEF_B : ")); // = 2988.64;			
	Serial.println(profile.THERMISTER_COEF_B);
	Serial.println();

	Serial.print(F("31 - DENOISE_ORDER: ")); // = 1;
	Serial.println(profile.DENOISE_ORDER);
	Serial.print(F("32 - DENOISE_DIFF_LIMIT : ")); // = 0.02;
	Serial.println(profile.DENOISE_DIFF_LIMIT);
	
	Serial.print(F("41 - CONTROLLER_PWM_MAX : ")); // = 255;
	Serial.println(profile.CONTROLLER_PWM_MAX);
	Serial.print(F("42 - CONTROLLER_PWM_PIN : ")); // = 6;
	Serial.println(profile.CONTROLLER_PWM_PIN);
	Serial.print(F("43 - CONTROLLER_INA_PIN : ")); // = 7;
	Serial.println(profile.CONTROLLER_INA_PIN);
	Serial.print(F("44 - CONTROLLER_INB_PIN : ")); // = 8;
	Serial.println(profile.CONTROLLER_INB_PIN);
	Serial.println();

	Serial.print(F("51 - kP : ")); // = 500.0; 
	Serial.println(profile.kP);
	Serial.print(F("52 - kI : ")); // = 0;
	Serial.println(profile.kI);
	Serial.print(F("53 - kD : ")); // = 0;
	Serial.println(profile.kD);
}


void command_load()
{
	if (task_isRunning()) {
		Serial.print(F("ERROR: Use 'stop' to stop control task first."));
	}
	else {
		Serial.print(F("Loading Profile from Storage... "));
		if (profile_load()) {
			Serial.print(F("DONE\r\n"));
			command_show();
		}
		else {
			Serial.print(F("FAIL!!\r\n"));
		}
	}
}


void command_save()
{
	if (task_isRunning()) {
		Serial.print(F("ERROR: Use 'stop' to stop control task first."));
	}
	else {
		Serial.print(F("Saving Profile to storage... "));
		if (profile_save()) {
			Serial.print(F("DONE\r\n"));
			command_show();
		}
		else {
			Serial.print(F("FAIL!!\r\n"));
		}
	}
}


void command_set()
{
	if (task_isRunning()) {
		Serial.print(F("ERROR: Use 'stop' to stop control task first."));
	}
	else {
		int key = 0;
		double value = 0;
		sscanf(command, "%*s%d%lf", &key);
		if (key >= 10 && key <= 99 && commandLength >=8) {
			value = atof(&command[7]);
			switch (key) {
				case 11: profile.setpointTemp = value; break;
				case 12: profile.isAutorunEnabled = (((int)value) == 0? 0:1); break;
				case 13: profile.isPlottingTaskEnabled = (((int)value) == 0? 0:1); break;
				case 21: profile.ADC_MAX = value; break;
				case 22: profile.THERMISTER_PIN = value; break;
				case 23: profile.THERMISTER_REF_RESISTER = value; break;
				case 24: profile.THERMISTER_ROOM_RESISTANCE = value; break;
				case 25: profile.THERMISTER_ROOM_TEMP = value; break;
				case 26: profile.THERMISTER_COEF_B = value; break;
				case 31: profile.DENOISE_ORDER = value; break;
				case 32: profile.DENOISE_DIFF_LIMIT = value; break;
				case 41: profile.CONTROLLER_PWM_MAX = value; break;
				case 42: profile.CONTROLLER_PWM_PIN = value; break;
				case 43: profile.CONTROLLER_INA_PIN = value; break;
				case 44: profile.CONTROLLER_INB_PIN = value; break;
				case 51: profile.kP = value; break;
				case 52: profile.kI = value; break;
				case 53: profile.kD = value; break;
				default: Serial.print(F("ERROR: Unknown key.")); return;			
			}
			Serial.print(F("DONE\r\n\r\n"));
		}
		else {
			Serial.print(F("ERROR: Unknown key and value.")); 
			return;	
		}

		command_show();
	}
}


bool profile_load()
{
	EEPROM.get(0, profile);
	return true;
}


bool profile_save()
{
	EEPROM.put(0, profile);
	return true;
}