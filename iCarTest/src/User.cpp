#include <board.h>
#include <stdio.h>

#define Threshold 2500

enum machineStatus{
	wait,
	run
};

enum driveStatus{
	stop,
	forward,
	left,
	right
};

enum buttonStatus{
	unpushed,
	pushed
};

typedef struct{
	enum buttonStatus prevState;
	enum buttonStatus nowState;
}Button;

enum machineStatus machineState;
enum driveStatus driveState;
Button button;

void setup() {

	machineState = wait;
	driveState = stop;
	button = {unpushed, unpushed};

	LcdDrv_clear();

}

void showSensorValue(){

	char str[17];

	int lineL = analogRead(PIN_LINE_L);
	int lineR = analogRead(PIN_LINE_R);

	LcdDrv_clear();

	// 左センサ値
	LcdDrv_setCursor(0,0);
	sprintf(str, "L:%4d", lineL);
	LcdDrv_print(str);

	// 右センサ値
	LcdDrv_setCursor(1,0);
	sprintf(str, "R:%4d", lineR);
	LcdDrv_print(str);

	LcdDrv_update();
}

void loop() {

	showSensorValue();

	delay(200);
}

#include <board.h>

#define BUTTONINTERVAL 100//ms
#define CarSpeed 50 //車の速度
#define CurveSpeedHigh 60//カーブ時のタイヤの回転数
#define CurveSpeedLow 0//カーブ時のタイヤの回転数
#define BackSpeed 0 //バックできない仕様
#define Threshold 2300 //判定閾値(０にしたら曲がらない=値が大きいほど黒の判定が厳しくなる)

enum machineStatus{
	wait,
	run
};

enum driveStatus{
	stop,
	forward,
	left,
	right,
	backRight,
	backLeft
};

enum buttonStatus{
	unpushed,
	pushed
};

typedef struct{
	enum buttonStatus prevState;
	enum buttonStatus nowState;
}Button;

enum machineStatus machineState;
enum driveStatus driveState;
Button button;
unsigned long showTime= 0;

void showStartMessage(){
	LcdDrv_clear();
	LcdDrv_setCursor(0,0);
	LcdDrv_print("System Stopping");
	LcdDrv_setCursor(1,0);
	LcdDrv_print("btn1: start");
	LcdDrv_update();
	delay(3000);
}

void setup() {
	showStartMessage();
	machineState = wait;
	driveState = stop;
	button = {unpushed, unpushed};
}


void inputButton(Button* b, machineStatus* ms){
    static enum machineStatus temp = wait;
    static unsigned long startTime = 0;

    b->nowState = unpushed;

    if(digitalRead(PIN_BTN1) == HIGH){
        temp = run;
        b->nowState = pushed;
    }

    if(digitalRead(PIN_BTN2) == HIGH){
        temp = wait;
        b->nowState = pushed;
    }

    if(b->nowState == pushed && b->prevState == unpushed){
        if(millis() - startTime > BUTTONINTERVAL){
            *ms = temp;
            startTime = millis();
        }
    }

    b->prevState = b->nowState;
}

void tracingLine(enum driveStatus* ds){
	int lineL = analogRead(PIN_LINE_L);
	int lineR = analogRead(PIN_LINE_R);
	//static enum driveStatus prevState = forward;

	/* 左右共に白なら */
	if ((lineL <= Threshold) && (lineR <= Threshold)) {
	/* 直進 */
		analogWrite(PIN_MOTOR_L, CarSpeed);
		analogWrite(PIN_MOTOR_R, CarSpeed);
		*ds = forward;

	/* 左センサが黒を検知したら */
	} else if ((lineL > Threshold) && (lineR <= Threshold)) {
		/* 左モータの速度を下げる */
		analogWrite(PIN_MOTOR_L, CurveSpeedLow);
		analogWrite(PIN_MOTOR_R, CurveSpeedHigh);
		*ds = left;

	/* 右センサが黒を検知したら */
    } else if ((lineL <= Threshold) && (lineR > Threshold)) {
    	/* 右モータの速度を下げる */
    	analogWrite(PIN_MOTOR_L, CurveSpeedHigh);
    	analogWrite(PIN_MOTOR_R, CurveSpeedLow);
    	*ds = right;

    /* それ以外なら */
    }else{
    	analogWrite(PIN_MOTOR_L, CarSpeed);
    	analogWrite(PIN_MOTOR_R, CarSpeed);
    	*ds = forward;
    	}

    //prevState = *ds;
}

void lightTurnSignal(enum driveStatus* ds){
	static unsigned long startTime = 0;
	static int signalState = 0;

	switch(*ds){
	case forward:
	case stop:
		digitalWrite(PIN_LED1,LOW);
		digitalWrite(PIN_LED2,LOW);
		signalState = 0;
		break;

	case backRight:
	case backLeft:
		digitalWrite(PIN_LED1,HIGH);
		digitalWrite(PIN_LED2,HIGH);
		signalState = 0;
		break;

	case left:
		if(signalState == 0 && millis() - startTime > 200){
			digitalWrite(PIN_LED1,HIGH);
			digitalWrite(PIN_LED2,LOW);
			signalState = 1;
			startTime = millis();
		}else if(signalState == 1 && millis()-startTime > 200){
			digitalWrite(PIN_LED1,LOW);
			digitalWrite(PIN_LED2,LOW);
			signalState = 0;
			startTime = millis();
		}
		break;

	case right:
		if(signalState == 0 && millis() - startTime > 200){
			digitalWrite(PIN_LED2,HIGH);
			digitalWrite(PIN_LED1,LOW);
			signalState = 1;
			startTime = millis();
		}else if(signalState == 1 && millis()-startTime > 200){
			digitalWrite(PIN_LED2,LOW);
			digitalWrite(PIN_LED1,LOW);
			signalState = 0;
			startTime = millis();
		}
		break;
	}

}

void soundBuzzer(enum driveStatus* ds){
	static unsigned long startTime=0;
	static int buzzerState=0;

	switch(*ds){
	case left:
	case right:
		if(buzzerState == 0 && millis()-startTime > 200){
			analogWrite(PIN_BUZZER, 2000);
			buzzerState = 1;
			startTime = millis();
		}else if(buzzerState == 1 && millis()-startTime > 50){
			analogWrite(PIN_BUZZER, 2000);
			buzzerState = 0;
			startTime = millis();
		}

		break;

	case backLeft:
	case backRight:
		if(buzzerState == 0 && millis()-startTime > 150){
			analogWrite(PIN_BUZZER, 2000);
			buzzerState = 1;
			startTime = millis();
		}else if(buzzerState == 1 && millis()-startTime > 150){
			analogWrite(PIN_BUZZER, 2000);
			buzzerState = 0;
			startTime = millis();
		}
		break;

	case stop:
	case forward:
	default:
		analogWrite(PIN_BUZZER,0);
		break;

	}
}

void showMachineStatus(enum machineStatus* ms){
	switch(*ms){
	case wait:
		LcdDrv_print("Wait");
		break;

	case run:
		LcdDrv_print("Run");
		break;
	}
}

void showDriveStatus(enum driveStatus* ds){
	switch(*ds){
	case stop:
		LcdDrv_print("Stop");
		break;
	case forward:
		LcdDrv_print("Front");
		break;
	case left:
		LcdDrv_print("Left");
		break;
	case right:
		LcdDrv_print("Right");
		break;
	case backLeft:
	case backRight:
		LcdDrv_print("Back");
		break;
	}
}

void showWait(){
	LcdDrv_clear();
	LcdDrv_setCursor(0,0);
	LcdDrv_print("System stopping");
	LcdDrv_setCursor(1,0);
	LcdDrv_print("Btn1 : Run");
	LcdDrv_update();
}

void showRun(enum driveStatus* ds){
	LcdDrv_clear();
	LcdDrv_setCursor(0,0);
	LcdDrv_print("status: ");
	showDriveStatus(ds);
	LcdDrv_setCursor(1,0);
	LcdDrv_print("Btn2 : Stop");
	LcdDrv_update();
}

void showLCD(enum driveStatus* ds, enum machineStatus* ms){
	static enum machineStatus prevMs = wait;

	switch(*ms){
	case wait:
		showWait();
		break;

	case run:
		showRun(ds);
		break;
	}

	prevMs = *ms;
}

void stopCar(){
	analogWrite(PIN_MOTOR_L, 0);
	analogWrite(PIN_MOTOR_R, 0);
}

void loop() {
	inputButton(&button, &machineState);
    if(machineState == run){
    	tracingLine(&driveState);
    	lightTurnSignal(&driveState);
    	soundBuzzer(&driveState);
    }else{
    	stopCar();
    }
    if(millis()-showTime > 200){
    	showTime = millis();
    	showLCD(&driveState, &machineState);
    }
}

