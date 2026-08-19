/*
 * SCSCL.cpp
 * 飞特SCSCL系列串行舵机应用层程序
 * 日期: 2024.4.2
 * 作者: 
 */

#include "SCSCL.h"

SCSCL::SCSCL()
{
	End = 1;
}

SCSCL::SCSCL(u8 End):SCSerial(End)
{
}

SCSCL::SCSCL(u8 End, u8 Level):SCSerial(End, Level)
{
}

int SCSCL::WritePos(u8 ID, u16 Position, u16 Time, u16 Speed)
{
	u8 bBuf[6];
	Host2SCS(bBuf+0, bBuf+1, Position);
	Host2SCS(bBuf+2, bBuf+3, Time);
	Host2SCS(bBuf+4, bBuf+5, Speed);
	
	return genWrite(ID, SCSCL_GOAL_POSITION_L, bBuf, 6);
}

int SCSCL::RegWritePos(u8 ID, u16 Position, u16 Time, u16 Speed)
{
	u8 bBuf[6];
	Host2SCS(bBuf+0, bBuf+1, Position);
	Host2SCS(bBuf+2, bBuf+3, Time);
	Host2SCS(bBuf+4, bBuf+5, Speed);
	
	return regWrite(ID, SCSCL_GOAL_POSITION_L, bBuf, 6);
}

void SCSCL::SyncWritePos(u8 ID[], u8 IDN, u16 Position[], u16 Time[], u16 Speed[])
{
    u8 offbuf[6*IDN];
    for(u8 i = 0; i<IDN; i++){
		u16 T, V;
		if(Time){
			T = Time[i];
		}else{
			T = 0;
		}
		if(Speed){
			V = Speed[i];
		}else{
			V = 0;
		}
        Host2SCS(offbuf+i*6+0, offbuf+i*6+1, Position[i]);
        Host2SCS(offbuf+i*6+2, offbuf+i*6+3, T);
        Host2SCS(offbuf+i*6+4, offbuf+i*6+5, V);
    }
    syncWrite(ID, IDN, SCSCL_GOAL_POSITION_L, offbuf, 6);
}

int SCSCL::EnableTorque(u8 ID, u8 Enable)
{
	return writeByte(ID, SCSCL_TORQUE_ENABLE, Enable);
}

int SCSCL::unLockEprom(u8 ID)
{
	return writeByte(ID, SCSCL_LOCK, 0);
}

int SCSCL::LockEprom(u8 ID)
{
	return writeByte(ID, SCSCL_LOCK, 1);
}

int SCSCL::FeedBack(int ID)
{
	int nLen = Read(ID, SCSCL_PRESENT_POSITION_L, Mem, sizeof(Mem));
	if(nLen!=sizeof(Mem)){
		return -1;
	}
	return nLen;
}
	
int SCSCL::ReadPos(int ID)
{
	int Pos = -1;
	if(ID==-1){
		Pos = Mem[SCSCL_PRESENT_POSITION_L-SCSCL_PRESENT_POSITION_L];
		Pos <<= 8;
		Pos |= Mem[SCSCL_PRESENT_POSITION_H-SCSCL_PRESENT_POSITION_L];
	}else{
		Pos = readWord(ID, SCSCL_PRESENT_POSITION_L);
	}
	return Pos;
}

int SCSCL::ReadSpeed(int ID)
{
	int Speed = -1;
	if(ID==-1){
		Speed = Mem[SCSCL_PRESENT_SPEED_L-SCSCL_PRESENT_POSITION_L];
		Speed <<= 8;
		Speed |= Mem[SCSCL_PRESENT_SPEED_H-SCSCL_PRESENT_POSITION_L];
	}else{
		Speed = readWord(ID, SCSCL_PRESENT_SPEED_L);
	}
	if(Speed&(1<<15)){
		Speed = -(Speed&~(1<<15));
	}	
	return Speed;
}

int SCSCL::ReadLoad(int ID)
{
	int Load = -1;
	if(ID==-1){
		Load = Mem[SCSCL_PRESENT_LOAD_L-SCSCL_PRESENT_POSITION_L];
		Load <<= 8;
		Load |= Mem[SCSCL_PRESENT_LOAD_H-SCSCL_PRESENT_POSITION_L];
	}else{
		Load = readWord(ID, SCSCL_PRESENT_LOAD_L);
	}
	if(Load&(1<<10)){
		Load = -(Load&~(1<<10));
	}	
	return Load;
}

int SCSCL::ReadVoltage(int ID)
{
	int Voltage = -1;
	if(ID==-1){
		Voltage = Mem[SCSCL_PRESENT_VOLTAGE-SCSCL_PRESENT_POSITION_L];	
	}else{
		Voltage = readByte(ID, SCSCL_PRESENT_VOLTAGE);
	}
	return Voltage;
}

int SCSCL::ReadTemper(int ID)
{
	int Temper = -1;
	if(ID==-1){
		Temper = Mem[SCSCL_PRESENT_TEMPERATURE-SCSCL_PRESENT_POSITION_L];	
	}else{
		Temper = readByte(ID, SCSCL_PRESENT_TEMPERATURE);
	}
	return Temper;
}

int SCSCL::ReadMove(int ID)
{
	int Move = -1;
	if(ID==-1){
		Move = Mem[SCSCL_MOVING-SCSCL_PRESENT_POSITION_L];	
	}else{
		Move = readByte(ID, SCSCL_MOVING);
	}
	return Move;
}

int SCSCL::ReadCurrent(int ID)
{
	int Current = -1;
	if(ID==-1){
		Current = Mem[SCSCL_PRESENT_CURRENT_L-SCSCL_PRESENT_POSITION_L];
		Current <<= 8;
		Current |= Mem[SCSCL_PRESENT_CURRENT_H-SCSCL_PRESENT_POSITION_L];
	}else{
		Current = readWord(ID, SCSCL_PRESENT_CURRENT_L);
	}
	if(Current&(1<<15)){
		Current = -(Current&~(1<<15));
	}	
	return Current;
}

int SCSCL::PWMMode(u8 ID)
{
	u8 bBuf[4];
	bBuf[0] = 0;
	bBuf[1] = 0;
	bBuf[2] = 0;
	bBuf[3] = 0;
	return genWrite(ID, SCSCL_MIN_ANGLE_LIMIT_L, bBuf, 4);	
}

int SCSCL::WritePWM(u8 ID, s16 pwmOut)
{
	if(pwmOut<0){
		pwmOut = -pwmOut;
		pwmOut |= (1<<10);
	}
	u8 bBuf[2];
	Host2SCS(bBuf+0, bBuf+1, pwmOut);
	
	return genWrite(ID, SCSCL_GOAL_TIME_L, bBuf, 2);
}

int SCSCL::SwitchMode(int ID, u8 mode)
{
	if(ID < 0 || ID > 0xff){
		return -1;
	}
	if(mode > 1){
		return -2;
	}

	const u8 servo_id = static_cast<u8>(ID);

	if(mode == 1){
		min_angle[servo_id] = readWord(servo_id, SCSCL_MIN_ANGLE_LIMIT_L);
		max_angle[servo_id] = readWord(servo_id, SCSCL_MAX_ANGLE_LIMIT_L);
		if(min_angle[servo_id] == -1 || max_angle[servo_id] == -1){
			return -3;
		}

		angle_limit_cached[servo_id] = true;
		return PWMMode(servo_id);
	}

	if(!angle_limit_cached[servo_id]){
		return -4;
	}
	if(writeWord(servo_id, SCSCL_MIN_ANGLE_LIMIT_L, (u16)min_angle[servo_id]) != 1){
		return -5;
	}
	if(writeWord(servo_id, SCSCL_MAX_ANGLE_LIMIT_L, (u16)max_angle[servo_id]) != 1){
		return -6;
	}

	return 0;
}

int SCSCL::ReadToqueEnable(int ID)
{
	return readWord(ID, SCSCL_TORQUE_ENABLE);
}
