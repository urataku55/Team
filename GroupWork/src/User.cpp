#include <board.h>
/***宣言***/
/**define**/
#define BTN_PERIOD 20					//ボタンチェック周期 ms秒
#define CHATTERING_DURATION 50			//チャタリング防止時間 ms秒
#define LONG_PRESS_TIME 3000			//長押し判定時間 ms秒
#define BTN_ON HIGH						//ボタンON
#define BTN_OFF LOW						//ボタンOFF
#define MOTOR_SPEEDL 51					//通常左モーター速度
#define MOTOR_SPEEDR 50					//通常右モーター速度
#define MAX_SPEED 63						//最大速度
#define STOP_LINE 1000					//停止する閾値(白)
/**enum**/
typedef enum {
	STATE_IDLE = 0,
	STATE_RUN = 1,
	STATE_STOP = 2,
}State;
State state;
/**構造体**/
/*走行用*/
typedef struct {
	int gap;							//誤差
	int control;						//P制御
	int sensorL;						//左センサー
	int sensorR;						//右センサー
	int leftSpeed;						//左モーター速度
	int rightSpeed;						//右モーター速度
	unsigned long runStartTime;			//走行開始時間
	unsigned long runTime;				//走行タイム
	bool timeFlag;						//計測中フラグ
	unsigned long outStartTime;			//コースアウト開始時間
	bool outFlag;						//コースアウトフラグ
	int lineCount;						//スタートラインを通過した回数
	unsigned long blackLineTime;		//黒ライン上の時間
	unsigned long lineTime;				//閾値が切り替わった時間
	bool onLine;						//ライン上に来たか
}RunState;
RunState runS;
/*ボタン用*/
typedef struct {
	int btnState;							//現在のボタン状態
	int btnLast;							//前回のボタン状態
	unsigned long changeTime;				//チャタリング用
	unsigned long pressTime;				//押下開始時間
	unsigned long pressDuration;			//押下経過時間
	unsigned long prev;						//前回実行した時刻
}BtnState;
BtnState btnS;
/**グローバル変数**/
unsigned long now = 0;		//現在時刻
/**プロトタイプ宣言**/
void checkBtn(BtnState* pBtnS, State* pState);
void runTimeMeasurement(RunState* pRunS, State* pState);
void statusDisplay(RunState* pRunS, State* pState);
void avoidCollision(State* pState);
bool checkGoal(RunState* pRunS);
/***セットアップ***/
void setup() {
	/*初期状態はIDLE*/
	state = STATE_IDLE;
	/*構造体の初期化*/
	RunState runS = {0};
	BtnState btnS = {0};
	/*ディスプレイの初期化*/
	LcdDrv_clear();
	LcdDrv_setCursor(0,0);
}
/***メイン処理***/
void loop() {
	/*現在時刻の取得*/
	now = millis();
	int baseSpeedL;              //左ベーススピード
	int baseSpeedR;              //右ベーススピード
	int diff;                   //急激な角度変化量に対応
	static int prevGap = 0;                //直前の角度のgap
	/**ボタンをチェックする(BTN_PERIOD ms秒)**/
	if(now - btnS.prev >= BTN_PERIOD) {
		btnS.prev = now;
		checkBtn(&btnS, &state);
	}

	/*5cm先にものがあったら停止する*/
	if(analogRead(PIN_DISTANCE) < 50){
		avoidCollision(&state);
		digitalWrite(PIN_LED3, HIGH);
	}else{
		digitalWrite(PIN_LED3, LOW);
	}

	/**タイム計測処理**/
	runTimeMeasurement(&runS, &state);
	/**動作処理**/
	switch(state) {
	/*待機状態*/
	case STATE_IDLE :
		analogWrite(PIN_MOTOR_L, 0);
		analogWrite(PIN_MOTOR_R, 0);
		break;
	/*走行状態*/
	case STATE_RUN :
		/*センサー読み込み*/
		runS.sensorL = analogRead(PIN_LINE_L);
		runS.sensorR = analogRead(PIN_LINE_R);
		/*ラインから外れたら停止する*/
		if(runS.sensorL < STOP_LINE && runS.sensorR < STOP_LINE) {
			/*外れ始め*/
			if(runS.outFlag == false) {
				runS.outStartTime = millis();
				runS.outFlag = true;
			}
			/*5秒経過したら停止*/
			if(millis() - runS.outStartTime >= 5000) {
				state = STATE_STOP;
				break;
			}
		}else {
			/*ラインに戻ったらリセット*/
			runS.outFlag = false;
		}
		/*3周したか確認しゴールしていたら停止させる*/
		if(checkGoal(&runS) == true) {
			state = STATE_STOP;
			break;
		}
		/*センサー誤差確認*/
		//左に重みを少し持たせている
		runS.gap = runS.sensorL+85 - runS.sensorR;//トレースセンサの誤差調整必要
		diff = runS.gap -prevGap;

		/*gapに応じて速度を連続変化*/
		//基本速度に直接影響
		baseSpeedL = MOTOR_SPEEDL - abs(runS.gap/60) - abs(diff/15);//80,20は調整必要
		baseSpeedR = MOTOR_SPEEDR - abs(runS.gap/60) - abs(diff/15);

		/*baseSpeedを上限下限値に調整*/
		baseSpeedL = constrain(baseSpeedL,15,MOTOR_SPEEDL);
		baseSpeedR = constrain(baseSpeedR,15,MOTOR_SPEEDR);

		/*P制御*/
		//曲がるときの左右の車輪の回転数に直接影響
		runS.control = runS.gap / 25 + diff / 7;//40,15も調整必要

		/*モーター速度調整*/
		runS.leftSpeed = baseSpeedL - runS.control;
		runS.rightSpeed = baseSpeedR + runS.control;

		//速度制限
		runS.leftSpeed = constrain(runS.leftSpeed,0,MAX_SPEED);
		runS.rightSpeed = constrain(runS.rightSpeed,0,MAX_SPEED);

		/*モーター始動*/
		analogWrite(PIN_MOTOR_L, runS.leftSpeed);
		analogWrite(PIN_MOTOR_R, runS.rightSpeed);

		prevGap = runS.gap;
		break;
	/*停止状態*/
	case STATE_STOP :
		analogWrite(PIN_MOTOR_L, 0);
		analogWrite(PIN_MOTOR_R, 0);
		break;
	/*例外*/
	default :
		analogWrite(PIN_MOTOR_L, 0);
		analogWrite(PIN_MOTOR_R, 0);
		break;
	}
	/**ディスプレイに表示**/
	statusDisplay(&runS, &state);
}
/***各種関数***/
/**ボタンで状態を切り替える関数**/
void checkBtn(BtnState* pBtnS, State* pState) {
	/*checkBtn内変数*/
	int reading = digitalRead(PIN_BTN1);
	/*チャタリング防止 50ms秒*/
	if(reading != pBtnS -> btnLast) {
		pBtnS -> btnLast = reading;
		pBtnS -> changeTime = now;
	}
	if((now - pBtnS -> changeTime) < CHATTERING_DURATION) {
		return;
	}
	/*状態変化チェック(変化なしの場合)*/
	if(reading == pBtnS -> btnState) {
		return;
	/*変化ありの場合*/
	}else {
		pBtnS -> btnState = reading;
	}
	/*押された瞬間*/
	if(pBtnS -> btnState == BTN_ON) {
		pBtnS -> pressTime = now;
		return;
	/*離された瞬間*/
	}else {
		pBtnS -> pressDuration = now - pBtnS -> pressTime;
	}
	/*状態遷移(3秒以上押下した場合)*/
	if(pBtnS -> pressDuration >= LONG_PRESS_TIME) {
		*pState = STATE_IDLE;
	/*アイドル状態の場合*/
	}else if(*pState == STATE_IDLE) {
		*pState = STATE_RUN;
	/*走行状態(それ以外)の場合*/
	}else {
		*pState = STATE_STOP;
	}
}
/**走行タイムを計測する関数**/
void runTimeMeasurement(RunState* pRunS, State* pState) {
	/*走行状態になったら計測開始*/
	if(pRunS -> timeFlag == false && *pState == STATE_RUN) {
		pRunS -> runStartTime = millis();
		pRunS -> timeFlag = true;
	}
	/*停止状態になったら計測終了*/
	if(pRunS -> timeFlag == true && *pState == STATE_STOP) {
		pRunS -> runTime = millis() - pRunS -> runStartTime;
		pRunS -> timeFlag = false;
	}
}
/**ディスプレイに状態を表示させる関数**/
void statusDisplay(RunState* pRunS, State* pState) {
	LcdDrv_clear();
	LcdDrv_setCursor(0, 0);
	unsigned int sec;
	unsigned int msec;
	char buf[20];
	switch(*pState) {
	/*待機状態*/
	case STATE_IDLE :
		LcdDrv_print("\xc0\xb2\xb7\xc1\xa9\xb3");		//ﾀｲｷﾁｭｳ
		break;
	/*走行状態*/
	case STATE_RUN :
		LcdDrv_print("\xbf\xb3\xba\xb3\xc1\xa9\xb3");	//ｿｳｺｳﾁｭｳ
		char buf[20];
		snprintf(buf, sizeof(buf), "CNT:%d", pRunS->lineCount);
		LcdDrv_setCursor(0,1);
		LcdDrv_print(buf);
		break;
	/*停止状態*/
	case STATE_STOP :
		sec = pRunS -> runTime / 1000;
		msec = (pRunS -> runTime % 1000) / 100;
		snprintf(buf, sizeof(buf), "%u.%01u s", sec, msec);
		LcdDrv_print("\xc3\xb2\xbc\xc1\xa9\xb3");		//ﾃｲｼﾁｭｳ
		LcdDrv_print(buf);
		break;
	/*例外*/
	default :
		LcdDrv_print("\xb4\xd7\xB0");					//ｴﾗｰ
		break;
	}
	LcdDrv_update();
}

void avoidCollision(State* pState){
	*pState = STATE_IDLE;
}
/**3周したか確認する関数**/
bool checkGoal(RunState* pRunS) {
	bool isBlackL = (pRunS -> sensorL) >= 2000;
	bool isBlackR = (pRunS -> sensorR) >= 2000;
	bool isWhiteL = (pRunS -> sensorL) <= 400;
	bool isWhiteR = (pRunS -> sensorR) <= 400;
	/*センサーの閾値が白と黒または黒と黒の場合その時間を記録する*/
	if((isBlackL && isWhiteR) || (isWhiteL && isBlackR) || (isBlackL && isBlackR)) {
		pRunS -> lineTime = millis();
	}
	/*センサーの閾値が両方黒かつ閾値が切り替わる時間が200ms秒以下ならゴールライン上にいるとみなす*/
	if(isBlackL && isBlackR && (millis() - pRunS -> lineTime <= 200) && pRunS -> onLine == false) {
		pRunS -> blackLineTime = millis();
		pRunS -> onLine = true;
	}
	/*ゴールライン上にいる時間が200ms秒を超えたらカウントを増やす*/
	if(millis() - pRunS -> blackLineTime >= 200 && pRunS -> onLine == true) {
		pRunS -> lineCount++;
		pRunS -> onLine = false;
	}
	/*3周していたらゴールフラグをtrueにする*/
	if(pRunS -> lineCount == 7) {
		pRunS -> lineCount = 0;
		return true;
	}
	return false;
}
