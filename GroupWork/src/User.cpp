#include <board.h>
/***宣言***/
/**define**/
#define BTN_PERIOD 20					//ボタンチェック周期 ms秒
#define CHATTERING_DURATION 50			//チャタリング防止時間 ms秒
#define LONG_PRESS_TIME 3000			//長押し判定時間 ms秒
#define BTN_ON HIGH						//ボタンON
#define BTN_OFF LOW						//ボタンOFF
#define MOTOR_SPEEDL 51					//通常左モーター速度
#define MOTOR_SPEEDR 50				//通常右モーター速度
#define MAX_SPEED 65						//最大速度
#define STOP_LINE 1500					//停止する閾値(白)
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
void checkBtn(BtnState* pBtnS, State* pState, RunState* pRunS);
void runTimeMeasurement(RunState* pRunS, State* pState);
void statusDisplay(RunState* pRunS, State* pState);
void avoidCollision(State* pState);
bool checkGoal(RunState* pRunS);
void changeDrivingMode(RunState* pRunS, State* pState);
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
	/**ボタンをチェックする(BTN_PERIOD ms秒)**/
	if(now - btnS.prev >= BTN_PERIOD) {
		btnS.prev = now;
		checkBtn(&btnS, &state, &runS);
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
	changeDrivingMode(&runS, &state);
	/**ディスプレイに表示**/
	statusDisplay(&runS, &state);
}


/***各種関数***/

void changeDrivingMode(RunState* pRunS, State* pState){
	int baseSpeedL;              //左ベーススピード
	int baseSpeedR;              //右ベーススピード
	int diff;                   //急激な角度変化量に対応
	static bool startFlag = true; //稼働時速度調整用
	static int prevGap = 0;                //直前の角度のgap
	static int integral = 0;    //積分値

	switch(*pState) {
		/*待機状態*/
		case STATE_IDLE :
			analogWrite(PIN_MOTOR_L, 0);
			analogWrite(PIN_MOTOR_R, 0);
			break;
		/*走行状態*/
		case STATE_RUN :
			if(startFlag){
				pRunS->sensorL = analogRead(PIN_LINE_L);
				pRunS->sensorR = analogRead(PIN_LINE_R);
				startFlag = false;
			}else{
				/*センサー読み込み*/
				pRunS->sensorL =
						(pRunS->sensorL*7 + analogRead(PIN_LINE_L)*3)/10;
				pRunS->sensorR =
						(pRunS->sensorR*7 + analogRead(PIN_LINE_R)*3)/10;
			}
			/*ラインから外れたら停止する*/
			if(pRunS->sensorL < STOP_LINE && pRunS->sensorR < STOP_LINE) {
				/*外れ始め*/
				if(pRunS->outFlag == false) {
					pRunS->outStartTime = millis();
					pRunS->outFlag = true;
				}
				/*3秒経過したら停止*/
				if(millis() - pRunS->outStartTime >= 3000) {
					state = STATE_STOP;
					break;
				}
			}else {
				/*ラインに戻ったらリセット*/
				pRunS->outFlag = false;
			}
			/*3周したか確認しゴールしていたら停止させる*/
			if(checkGoal(&runS) == true) {
				state = STATE_STOP;
				break;
			}
			/*センサー誤差確認*/
			//左に重みを少し持たせている
			pRunS->gap = pRunS->sensorL+85 - pRunS->sensorR;//トレースセンサの誤差調整必要
			diff = pRunS->gap -prevGap;
			integral += pRunS->gap;
			integral = constrain(integral, -2000, 2000);

			/*gapに応じて速度を連続変化*/
			//基本速度に直接影響
			baseSpeedL = MOTOR_SPEEDL - abs(pRunS->gap/60) - abs(diff/15);//80,20は調整必要
			baseSpeedR = MOTOR_SPEEDR - abs(pRunS->gap/60) - abs(diff/15);

			/*baseSpeedを上限下限値に調整*/
			baseSpeedL = constrain(baseSpeedL,15,MOTOR_SPEEDL);
			baseSpeedR = constrain(baseSpeedR,15,MOTOR_SPEEDR);

			/*P制御*/
			//曲がるときの左右の車輪の回転数に直接影響
			pRunS->control = pRunS->gap / 10 + diff / 5 + integral / 400 ;//40,15も調整必要

			/*モーター速度調整*/
			pRunS->leftSpeed = baseSpeedL - pRunS->control;
			pRunS->rightSpeed = baseSpeedR + pRunS->control;

			//速度制限
			pRunS->leftSpeed = constrain(pRunS->leftSpeed,0,MAX_SPEED);
			pRunS->rightSpeed = constrain(pRunS->rightSpeed,0,MAX_SPEED);

			/*モーター始動*/
			analogWrite(PIN_MOTOR_L, pRunS->leftSpeed);
			analogWrite(PIN_MOTOR_R, pRunS->rightSpeed);

			prevGap = pRunS->gap;
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
}

/**ボタンで状態を切り替える関数**/
void checkBtn(BtnState* pBtnS, State* pState, RunState* pRunS) {

	pBtnS -> btnLast = pBtnS -> btnState;//直前の入力をbtnLastに格納

	/*checkBtn内変数*/
	int reading; //0:入力なし　1：ボタン１　2:ボタン２
	if(digitalRead(PIN_BTN1) == true){
		reading = 1;
	}else if(digitalRead(PIN_BTN2) == true){
		reading = 2;
	}else{
		reading = 0;
	}

	/*チャタリング防止 50ms秒*/
	if((now - pBtnS -> changeTime) < CHATTERING_DURATION) {
		return;
	}

	//現在の入力をbtnStateに格納
	pBtnS -> btnState = reading;

	//入力が変更されたらchangeTime更新
	/*状態変化チェック(変化ありの場合)*/
	if(pBtnS -> btnState != pBtnS -> btnLast) {
		pBtnS -> changeTime = now;
	}else{
		/*状態変化チェック(変化なしの場合)*/
		return;
	}

	if(pBtnS -> btnState == 1 && *pState != STATE_RUN){
	    pRunS->lineCount = 0;
	    pRunS->onLine = false;
	    pRunS->blackLineTime = 0;
	    pRunS->lineTime = 0;
	    pRunS->outFlag = false;
		*pState = STATE_RUN;
	}

	if(pBtnS -> btnState == 2){
	    pRunS->lineCount = 0;
	    pRunS->onLine = false;
	    pRunS->blackLineTime = 0;
	    pRunS->lineTime = 0;
	    pRunS->outFlag = false;
		*pState = STATE_STOP;
	}
}
/**走行タイムを計測する関数**/
void runTimeMeasurement(RunState* pRunS, State* pState) {

	/*走行状態になったら計測開始*/
	if(pRunS -> timeFlag == false && *pState == STATE_RUN) {
		pRunS -> runStartTime = now;
		pRunS -> timeFlag = true;
	}

	/*RUN中は常時更新*/
	if(*pState == STATE_RUN){
		pRunS -> runTime = now - pRunS -> runStartTime;
	}

	/*停止状態になったら計測終了*/
	if(pRunS -> timeFlag == true && *pState == STATE_STOP) {
		pRunS -> timeFlag = false;
	}
}

/**ディスプレイに状態を表示させる関数**/
void statusDisplay(RunState* pRunS, State* pState) {

    unsigned int sec;
    unsigned int msec;
    char buf[20];

    /* 時間文字列作成 */
    sec  = pRunS->runTime / 1000;
    msec = (pRunS->runTime % 1000) / 100;

    snprintf(buf, sizeof(buf), "TIME:%u.%01u", sec, msec);

    /* LCDクリア */
    LcdDrv_clear();
    switch(*pState) {

    /* 待機状態 */
    case STATE_IDLE :
        LcdDrv_setCursor(0,0);
        LcdDrv_print("IDLE");
        LcdDrv_setCursor(1,0);
        LcdDrv_print("Button1: Run");
        break;

    /* 走行状態 */
    case STATE_RUN :
        LcdDrv_setCursor(0,0);
        LcdDrv_print("RUN /Btn2:Stop");
        LcdDrv_setCursor(1,0);
        LcdDrv_print(buf);
        break;

    /* 停止状態 */
    case STATE_STOP :
        LcdDrv_setCursor(0,0);
        LcdDrv_print("STOP /Btn1:Run");
        LcdDrv_setCursor(1,0);
        LcdDrv_print(buf);
        break;

    /* エラー */
    default :
        LcdDrv_setCursor(0,0);
        LcdDrv_print("ERROR");
        break;
    }

    /* LCD反映 */
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
	if(isBlackL && isBlackR && (millis() - pRunS -> lineTime <= 300) && pRunS -> onLine == false) {
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
