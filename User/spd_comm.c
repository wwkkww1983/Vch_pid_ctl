#include "spd_comm.h"

extern short wReg[];

//-------------------------------------------------------------------------------
//	@brief	速度值計算隊列初始化
//	@param	svq:隊列指針
//	@retval	None
//-------------------------------------------------------------------------------
void SpdQueueInit(SpeedValueQueue *svq)
{
    int i;

    svq->ptr_head = 0;
    svq->ptr_tail = 0;
    svq->lSum_ang = 0;
    svq->lSum_tim = 0;
    for (i = 0; i < SPD1_QUEUE_LEN; i++)
    {
        svq->queue_ang[i] = 0;
        svq->queue_tim[i] = 0;
    }
}

//-------------------------------------------------------------------------------
//	@brief	速度值計算隊列初始化
//	@param	svq:隊列指針
//          val:插入隊列的值
//	@retval	None
//-------------------------------------------------------------------------------
void SpdQueueIn(SpeedValueQueue *svq, short ang, short tim)
{
    svq->lSum_ang += ang;
    svq->lSum_ang -= svq->queue_ang[svq->ptr_head];
    svq->lSum_tim += tim;
    svq->lSum_tim -= svq->queue_tim[svq->ptr_head];

    svq->queue_ang[svq->ptr_head] = ang;
    svq->queue_tim[svq->ptr_head] = tim;

    svq->ptr_head++;
    if (svq->ptr_head >= SPD1_QUEUE_LEN)
        svq->ptr_head = 0;
}

//-------------------------------------------------------------------------------
//	@brief	速度值計算隊列初始化
//	@param	svq:隊列指針
//	@retval	隊列中保存數據的平均值
//-------------------------------------------------------------------------------
short SpdQueueAvgVal(SpeedValueQueue *svq)
{
    if (svq->lSum_tim == 0)
        return 0;
    return svq->lSum_ang * 1000 / svq->lSum_tim;
}

/****************************************************************
 *	@brief	PID模块初始化
 *	@param	pPid模块指针
 *          no 参数起始地址
 *	@retval	None
 ****************************************************************/
void PIDMod_initialize(PID_Module *pPid, int no)
{
    pPid->pParaAdr = &wReg[no];

    pPid->usLastFunc = 0;

    pPid->vOutL1 = 0;
    pPid->vOutL2 = 0;
    pPid->sDeltaL1 = 0;
    pPid->sDeltaL2 = 0;
}

/****************************************************************
 *	@brief	PID模块参数检测
 *	@param	pPid模块指针
 *          no 参数起始地址
 *	@retval	None
 ****************************************************************/
void PIDMod_update_para(PID_Module *pPid)
{
    if (pPid->pParaAdr[0] > 130 || pPid->pParaAdr[0] < 0) //判断输入寄存器地址
        pPid->pParaAdr[0] = 0;
    if (pPid->pParaAdr[1] >= 200 || pPid->pParaAdr[1] < 100) //判断输出寄存器地址
        pPid->pParaAdr[1] = 199;

    if (pPid->pParaAdr[7] < 0)
        pPid->pParaAdr[7] = 0;
    if (pPid->pParaAdr[7] > 100)
        pPid->pParaAdr[7] = 100;

    if (pPid->pParaAdr[9] < 0 || pPid->pParaAdr[9] > 3)
        pPid->pParaAdr[9] = 0;
}

/****************************************************************
 *	@brief	PID模块计算
 *	@param	pPid模块指针
 *	@retval	None
 ****************************************************************/
void PIDMod_step(PID_Module *pPid)
{
    long int pid_u, pid_out;
    long int curDelta, tmp, val;

    if (pPid->pParaAdr[9] == 0 && pPid->usLastFunc != 0)
    {
        wReg[pPid->pParaAdr[1]] = 0x8000; //单回路PID
        if (pPid->usLastFunc == 2 || pPid->usLastFunc == 3)
            wReg[pPid->pParaAdr[1] + 1] = 0x8000; //正并联或反并联

        pPid->sDeltaL1 = 0;
        pPid->sDeltaL2 = 0;

        pPid->vOutL2 = 0;
        pPid->vOutL1 = 0;
    }
    pPid->usLastFunc = pPid->pParaAdr[9];

    if (pPid->pParaAdr[9] == 0)
        return;

    curDelta = pPid->pParaAdr[3] - wReg[pPid->pParaAdr[0]]; //当前偏差值

    pid_u = pPid->pParaAdr[4] * (curDelta - pPid->sDeltaL1) +
            pPid->pParaAdr[5] * pPid->sDeltaL1 +
            pPid->pParaAdr[6] * (curDelta - 2 * pPid->sDeltaL1 + pPid->sDeltaL2);
    pPid->sDeltaL2 = pPid->sDeltaL1;
    pPid->sDeltaL1 = curDelta;

    pid_out = pPid->vOutL1;
    if (pPid->pParaAdr[8] == 0) //根据作用方式确定是增量还是减量
        pid_out -= pid_u;
    else
        pid_out += pid_u;

    //输出值限幅，避免调节器饱和
    if (pid_out > PID_MAX_OUT)
        pid_out = PID_MAX_OUT;
    if (pid_out < -PID_MAX_OUT)
        pid_out = -PID_MAX_OUT;

    //输出限幅
    tmp = 0x8000 * pPid->pParaAdr[7] / 100 - 1;
    val = pid_out / 1000;
    if (val > tmp)
        val = tmp;
    if (val < -tmp)
        val = -tmp;

    //死区调整
    tmp = (int)pPid->pParaAdr[2];
    if (val <= wReg[PID_ZERO_ZONE] && val >= -wReg[PID_ZERO_ZONE])
        val = 0;
    if (val > wReg[PID_ZERO_ZONE])
        val = tmp + val * (0x7FFF - tmp) / 0x7FFF;
    if (val < -wReg[PID_ZERO_ZONE])
        val = -tmp + val * (0x8000 - tmp) / 0x8000;

    //输出方式选择
    val &= 0x0000FFFF;
    wReg[pPid->pParaAdr[1]] = 0x8000 + val; //单回路PID

    //输出方式选择
    if (pPid->pParaAdr[9] == 2)
        wReg[pPid->pParaAdr[1] + 1] = 0x8000 + val; //正向并联PID
    if (pPid->pParaAdr[9] == 3)
        wReg[pPid->pParaAdr[1] + 1] = 0x8000 - val; //反向并联PID

    //保存中间结果
    pPid->vOutL2 = pPid->vOutL1;
    pPid->vOutL1 = pid_out;
}

/****************************************************************
 *	@brief	推进器模块的计算
 *	@param	pPid模块指针
 *	@retval	None
 ****************************************************************/
void Thruster_step(PID_Module *pPid)
{
    long int pid_u, pid_out;
    long int curDelta, tmp, val;
    float fin, fout;

    if (pPid->pParaAdr[9] == 0 && pPid->usLastFunc != 0)
    {
        wReg[pPid->pParaAdr[1]] = 0x8000; //单回路PID
        if (pPid->usLastFunc == 2 || pPid->usLastFunc == 3)
            wReg[pPid->pParaAdr[1] + 1] = 0x8000; //正并联或反并联

        pPid->sDeltaL1 = 0;
        pPid->sDeltaL2 = 0;

        pPid->vOutL2 = 0;
        pPid->vOutL1 = 0;
    }
    pPid->usLastFunc = pPid->pParaAdr[9];

    if (pPid->pParaAdr[9] == 0)
        return;

    curDelta = pPid->pParaAdr[3] - wReg[pPid->pParaAdr[0]]; //当前偏差值 设定值-实际值

    pid_u = pPid->pParaAdr[4] * (curDelta - pPid->sDeltaL1) +
            pPid->pParaAdr[5] * pPid->sDeltaL1 +
            pPid->pParaAdr[6] * (curDelta - 2 * pPid->sDeltaL1 + pPid->sDeltaL2);
    pPid->sDeltaL2 = pPid->sDeltaL1;
    pPid->sDeltaL1 = curDelta;

    pid_out = pPid->vOutL1;
    if (pPid->pParaAdr[8]) //根据作用方式确定是增量还是减量
        pid_out += pid_u;  //正作用
    else
        pid_out -= pid_u; //反作用

    //输出值限幅，避免调节器饱和
    //输出的最大推进力为130kgf
    if (pid_out > 1300000)
        pid_out = 1300000;
    if (pid_out < -1300000)
        pid_out = -1300000;

    //输出限幅
    tmp = pid_out * pPid->pParaAdr[7] / 100;

    //缩放到-130 - 130范围内
    wReg[161] = tmp / 10000;
    fin = (float)(wReg[161] > 0 ? wReg[161] : -wReg[161]);

    //根据推进力曲线，将推力转化到输出电压
    //Voltage(v):2  	3  		4       5       6       7       8       9       10
    //Force(kgf):2.0  3.0  4.5     9.8     21.7    46.3    70.8    97.2    130
    //根据拟合曲线计算
    // vol = 0.0294*f^3 - 7.057*f^2 + 609.4*f + 8100.0
    fout = fin * 0.0294f - 7.057f;
    fout = fin * fout + 609.4f;
    fout = fin * fout + 8100.0f;

    if (wReg[161] < 2 && wReg[161] > -2)
        fout = 0.0f;

    wReg[162] = (int)fout;

    if (wReg[161] > 0)
        val = (int)fout;
    else
        val = -(int)fout;

    if (val > 32767)
        val = 32767;
    if (val < -32767)
        val = -32767;

    //根据作用方式及偏差确定是否关断输出
    if (pPid->pParaAdr[8])      //正作用
    {
        //正作用，出现正偏差,且发生负输出时，输出为正回正电压
        if (curDelta > 0 && val < 0)
            val = wReg[164];
        //正作用，出现负偏差,且发生正输出时，输出为负回正电压
        if (curDelta < 0 && val > 0)
            val = -wReg[164];
    }
    else    //反作用
    {
        //出现正偏差,且发生正输出时，输出为负回正电压
        if (curDelta > 0 && val > 0)
            val = -wReg[164];
        //出现负偏差,且发生负输出时，输出为正回正电压
        if (curDelta < 0 && val < 0)
            val = wReg[164];
    }

    //在小偏差范围内，关断输出
    if (curDelta < wReg[163] && curDelta > -wReg[163])
        val = 0;

    wReg[165] = val;

    //输出方式选择
    val &= 0x0000FFFF;
    wReg[pPid->pParaAdr[1]] = 0x8000 + val; //单回路PID

    //输出方式选择
    if (pPid->pParaAdr[9] == 2)
        wReg[pPid->pParaAdr[1] + 1] = 0x8000 + val; //正向并联PID
    if (pPid->pParaAdr[9] == 3)
        wReg[pPid->pParaAdr[1] + 1] = 0x8000 - val; //反向并联PID

    //保存中间结果
    pPid->vOutL2 = pPid->vOutL1;
    pPid->vOutL1 = pid_out;
}

/*------------------end of file------------------------*/
