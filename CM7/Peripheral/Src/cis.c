/**
 ******************************************************************************
 * @file           : cis_BW_.c
 * @brief          :
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"
#include "main.h"
#include "config.h"
#include "basetypes.h"
#include "tim.h"
#include "adc.h"
#include "buttons.h"

#include "arm_math.h"

#include "stdlib.h"
#include "stdio.h"
#include "stdbool.h"

#include "ssd1362.h"

#include "cis.h"

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
//static int32_t *cisData = NULL;
//static int32_t *cisDataCpy = NULL;

static int32_t cisData[CIS_ADC_BUFF_SIZE * 3] = {0};
static int32_t cisDataCpy[CIS_ADC_BUFF_SIZE * 3] = {0};
static int8_t cisDataNegate[CIS_PIXELS_NB * sizeof(uint32_t)] = {255};

#ifdef CIS_BW
static int32_t *cisWhiteCalData     = NULL;
static int32_t *cisBlackCalData     = NULL;
static int32_t *cisOffsetCalData    = NULL;
static float32_t *cisGainsCalData 	= NULL;

static int32_t minWhitePix = 0;
static int32_t maxWhitePix = 0;

static const uint32_t blackCalibVirtAddVar = ADDR_FLASH_SECTOR_6_BANK1;
static const uint32_t whiteCalibVirtAddVar = ADDR_FLASH_SECTOR_7_BANK1;
#else

#endif

static volatile CIS_BUFF_StateTypeDef  cisHalfBufferState[3] = {0};
static volatile CIS_BUFF_StateTypeDef  cisFullBufferState[3] = {0};

/* Variable containing ADC conversions data */

/* Private function prototypes -----------------------------------------------*/
void cis_StartCalibration(uint32_t userFlashStartAddress, uint16_t iterationNb);
void cis_LoadCalibration(uint32_t userFlashStartAddress, int32_t *cisCalData, int32_t *minPix, int32_t *maxPix);
void cis_TIM_CLK_Init(void);
void cis_TIM_SP_Init(void);
void cis_TIM_LED_R_Init(void);
void cis_TIM_LED_G_Init(void);
void cis_TIM_LED_B_Init(void);

void cis_ADC_Init(void);
void cis_ImageFilterBW(int32_t *cis_buff);

static void cis_CalibrationMenu(void);

/* Private user code ---------------------------------------------------------*/

/**
 * @brief  CIS init
 * @param  Void
 * @retval None
 */
void cis_Init(void)
{
	printf("----------- CIS INIT ----------\n");
	printf("-------------------------------\n");

	printf("CIS END CAPTURE = %d\n", CIS_LINE_SIZE);

	// Enable 5V power DC/DC for display
	HAL_GPIO_WritePin(EN_5V_GPIO_Port, EN_5V_Pin, GPIO_PIN_SET);

#ifdef CIS_BW
	// Allocate the contiguous memory area for storage cis datas
	cisData = malloc(CIS_ADC_BUFF_SIZE * sizeof(uint32_t));
	if (cisData == NULL)
	{
		Error_Handler();
	}

	// Clear buffers
	memset(cisData, 0, CIS_ADC_BUFF_SIZE * sizeof(uint32_t));

	cisWhiteCalData = malloc(CIS_EFFECTIVE_PIXELS * sizeof(uint32_t));
	if (cisData == NULL)
	{
		Error_Handler();
	}
	cisBlackCalData = malloc(CIS_EFFECTIVE_PIXELS * sizeof(uint32_t));
	if (cisData == NULL)
	{
		Error_Handler();
	}
	cisOffsetCalData = malloc(CIS_EFFECTIVE_PIXELS * sizeof(uint32_t));
	if (cisData == NULL)
	{
		Error_Handler();
	}
	cisGainsCalData = malloc(CIS_EFFECTIVE_PIXELS * sizeof(float32_t));
	if (cisData == NULL)
	{
		Error_Handler();
	}

	// Clear buffers
	memset(cisWhiteCalData, 0, CIS_EFFECTIVE_PIXELS * sizeof(uint32_t));
	memset(cisBlackCalData, 0, CIS_EFFECTIVE_PIXELS * sizeof(uint32_t));
	memset(cisOffsetCalData, 0, CIS_EFFECTIVE_PIXELS * sizeof(uint32_t));
	memset(cisGainsCalData, 0, CIS_EFFECTIVE_PIXELS * sizeof(float32_t));
#else
	memset(&cisData[0], 0, CIS_ADC_BUFF_SIZE * 3 * sizeof(uint32_t));

	// Allocate the contiguous memory area for storage cis datas
	//	cisData = malloc(CIS_ADC_BUFF_SIZE * 3 * sizeof(uint32_t));
	//	if (cisData == NULL)
	//	{
	//		Error_Handler();
	//	}
	//
	//	// Clear buffers
	//	memset(cisData, 0, CIS_ADC_BUFF_SIZE * 3 * sizeof(uint32_t));
#endif

#ifdef CIS_400DPI
	HAL_GPIO_WritePin(CIS_RS_GPIO_Port, CIS_RS_Pin, GPIO_PIN_RESET); //SET : 200DPI   RESET : 400DPI
#else
	HAL_GPIO_WritePin(CIS_RS_GPIO_Port, CIS_RS_Pin, GPIO_PIN_SET); //SET : 200DPI   RESET : 400DPI
#endif

	cis_ADC_Init();
	cis_TIM_SP_Init();
	cis_TIM_LED_R_Init();
	cis_TIM_LED_G_Init();
	cis_TIM_LED_B_Init();
	cis_TIM_CLK_Init();

	cis_Start_capture();

	//	if (buttonState[SW1] == SWITCH_PRESSED)
	//	{
	//		cis_CalibrationMenu();
	//	}

	//		cis_CalibrationMenu();

#ifdef CIS_BW
	// Load levels and compute calibration gains and offset for BW

	/*-------- 1 --------*/
	ssd1362_drawRect(0, DISPLAY_HEAD_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_HEAD_Y2POS, 4, TRUE);
	ssd1362_drawString(10, DISPLAY_HEAD_Y1POS + 1, (int8_t *)"LOAD WHITE LEVELS", 0xF, 8);
	ssd1362_writeFullBuffer();

	// Get the white levels
	cis_LoadCalibration(whiteCalibVirtAddVar, cisWhiteCalData, &minWhitePix, &maxWhitePix);

	// Print curve
	ssd1362_drawRect(0, DISPLAY_AERA1_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_AERA1_Y2POS + 1, 7, false);
	for (uint32_t i = 0; i < (DISPLAY_MAX_X_LENGTH); i++)
	{
		int32_t cis_color = cisWhiteCalData[(uint32_t)(i * ((float)(CIS_EFFECTIVE_PIXELS) / (float)DISPLAY_MAX_X_LENGTH))] >> 11;
		ssd1362_drawPixel(DISPLAY_MAX_X_LENGTH - 1 - i, DISPLAY_AERA1_Y2POS - cis_color, 15, false);
	}
	ssd1362_writeFullBuffer();

	/*-------- 2 --------*/
	ssd1362_drawRect(0, DISPLAY_HEAD_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_HEAD_Y2POS, 4, TRUE);
	ssd1362_drawString(10, DISPLAY_HEAD_Y1POS + 1, (int8_t *)"EXTRACT DIFFERENTIAL OFFSETS", 0xF, 8);
	ssd1362_writeFullBuffer();

	// Extract differential offsets
	for (uint32_t i = 0; i < CIS_EFFECTIVE_PIXELS; i++)
	{
		cisOffsetCalData[i] = maxWhitePix - cisWhiteCalData[i];
	}

	// Print curve
	for (uint32_t i = 0; i < (DISPLAY_MAX_X_LENGTH); i++)
	{
		uint32_t index = i * ((float)(CIS_EFFECTIVE_PIXELS) / (float)DISPLAY_MAX_X_LENGTH);
		int32_t cis_color = (cisOffsetCalData[index] + cisWhiteCalData[index]) >> 11;
		ssd1362_drawPixel(DISPLAY_MAX_X_LENGTH - 1 - i, DISPLAY_AERA1_Y2POS - cis_color, 3, false);
	}
	ssd1362_writeFullBuffer();


	/*-------- 3 --------*/
	ssd1362_drawRect(0, DISPLAY_HEAD_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_HEAD_Y2POS, 4, TRUE);
	ssd1362_drawString(10, DISPLAY_HEAD_Y1POS + 1, (int8_t *)"LOAD BLACK LEVELS", 0xF, 8);
	ssd1362_writeFullBuffer();

	// Get the black levels
	cis_LoadCalibration(blackCalibVirtAddVar, cisBlackCalData, NULL, NULL);

	// Print curve
	for (uint32_t i = 0; i < (DISPLAY_MAX_X_LENGTH); i++)
	{
		int32_t cis_color = cisBlackCalData[(uint32_t)(i * ((float)(CIS_EFFECTIVE_PIXELS) / (float)DISPLAY_MAX_X_LENGTH))] >> 11;
		ssd1362_drawPixel(DISPLAY_MAX_X_LENGTH - 1 - i, DISPLAY_AERA1_Y2POS - cis_color, 0, false);
	}
	ssd1362_writeFullBuffer();


	/*-------- 4 --------*/
	ssd1362_drawRect(0, DISPLAY_HEAD_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_HEAD_Y2POS, 4, TRUE);
	ssd1362_drawString(10, DISPLAY_HEAD_Y1POS + 1, (int8_t *)"COMPUTE COMPENSATIION GAINS", 0xF, 8);
	ssd1362_writeFullBuffer();

	// Compute gains
	for (uint32_t i = 0; i < CIS_EFFECTIVE_PIXELS; i++)
	{
		cisGainsCalData[i] = (float32_t)(65535 - 5535 - maxWhitePix) / (float32_t)(cisBlackCalData[i] - cisWhiteCalData[i]);
	}

	printf("-------- COMPUTE GAINS --------\n");
#ifdef PRINT_CIS_CALIBRATION
	for (uint32_t pix = 0; pix < CIS_EFFECTIVE_PIXELS; pix++)
	{
		printf("Pix = %d, Val = %0.3f\n", (int)pix, (float)cisGainsCalData[pix]);
	}
#endif
#endif
	//	HAL_Delay(1000);
	printf("-------------------------------\n");
}

/**
 * @brief  Return buffer data
 * @param  index
 * @retval value
 */
uint32_t cis_GetBuffData(uint32_t index)
{
	//	if (index < CIS_EFFECTIVE_PIXELS_PER_LINE)
	//		return cisData[index + CIS_START_OFFSET];
	//	if (index < CIS_EFFECTIVE_PIXELS_PER_LINE * 2)
	//		return cisData[index - CIS_EFFECTIVE_PIXELS_PER_LINE + CIS_START_OFFSET + CIS_ADC_BUFF_END_CAPTURE];
	//	if (index < CIS_EFFECTIVE_PIXELS_PER_LINE * 3)
	//		return cisData[index - (CIS_EFFECTIVE_PIXELS_PER_LINE * 2) + CIS_START_OFFSET + (CIS_ADC_BUFF_END_CAPTURE * 2)];
	return 0;
}

/**
 * @brief  Manages Image compute.
 * @param
 * @retval None
 */
void cis_ImageComputeBW(int32_t dataOffset, int32_t imageOffset, int32_t *cis_buff)
{
	/* Invalidate Data Cache to get the updated content of the SRAM on the first half of the ADC converted data buffer */
	SCB_InvalidateDCache_by_Addr((uint32_t *)&cisData[dataOffset] , CIS_PIXELS_PER_LINE * 2);

#ifndef CIS_DESACTIVATE_CALIBRATION
	arm_sub_q31(&cisData[dataOffset], &cisWhiteCalData[imageOffset], &cis_buff[imageOffset], CIS_EFFECTIVE_PIXELS_PER_LINE / 2);
	arm_mult_f32((float32_t *)(&cis_buff[imageOffset]), &cisGainsCalData[imageOffset],(float32_t *)(&cis_buff[imageOffset]), CIS_EFFECTIVE_PIXELS_PER_LINE / 2);
	arm_offset_q31(&cis_buff[imageOffset], maxWhitePix / 2, &cis_buff[imageOffset], CIS_EFFECTIVE_PIXELS_PER_LINE / 2);
#else
	arm_copy_q31(&cisData[dataOffset], &cis_buff[imageOffset], CIS_PIXELS_PER_LINE / 2);
#endif
	cis_ImageFilterBW(&cis_buff[imageOffset]);
}

/**
 * @brief  Manages Image compute.
 * @param
 * @retval None
 */
void cis_ImageComputeRGB(int32_t dataOffset, int32_t imageOffset, int32_t *cis_buff)
{
	/* Invalidate Data Cache to get the updated content of the SRAM on the first half of the ADC converted data buffer */
	SCB_InvalidateDCache_by_Addr((uint32_t *)&cisData[dataOffset] , CIS_PIXELS_PER_LINE * 2);

#ifndef CIS_DESACTIVATE_CALIBRATION
	arm_sub_q31(&cisData[dataOffset], &cisWhiteCalData[imageOffset], &cis_buff[imageOffset], CIS_EFFECTIVE_PIXELS_PER_LINE / 2);
	arm_mult_f32((float32_t *)(&cis_buff[imageOffset]), &cisGainsCalData[imageOffset],(float32_t *)(&cis_buff[imageOffset]), CIS_EFFECTIVE_PIXELS_PER_LINE / 2);
	arm_offset_q31(&cis_buff[imageOffset], maxWhitePix / 2, &cis_buff[imageOffset], CIS_EFFECTIVE_PIXELS_PER_LINE / 2);
#else
	arm_copy_q31(&cisData[dataOffset], &cis_buff[imageOffset], CIS_PIXELS_PER_LINE / 2);
#endif
	cis_ImageFilterBW(&cis_buff[imageOffset]);
}


/**
 * @brief  Manages Image process.
 * @param
 * @retval None
 */
void cis_ImageProcessBW(int32_t *cis_buff)
{
	static int32_t dataOffset, imageOffset;

	for (int32_t line = CIS_ADC_OUT_LINES; --line >= 0;)
	{
		/* 1st half buffer played; so fill it and continue playing from bottom*/
		while(cisHalfBufferState[line] != CIS_BUFFER_OFFSET_HALF);

		dataOffset = (CIS_STOP_OFFSET * line) + CIS_START_OFFSET;
		imageOffset = (CIS_PIXELS_PER_LINE * line);
		cis_ImageComputeBW(dataOffset, imageOffset, cis_buff);

		cisHalfBufferState[line] = CIS_BUFFER_OFFSET_NONE;


		/* 2nd half buffer played; so fill it and continue playing from top */
		while(cisFullBufferState[line] != CIS_BUFFER_OFFSET_FULL);

		dataOffset = (CIS_STOP_OFFSET * line) + CIS_START_OFFSET + (CIS_PIXELS_PER_LINE / 2);
		imageOffset = (CIS_PIXELS_PER_LINE * line) + (CIS_PIXELS_PER_LINE / 2);
		cis_ImageComputeBW(dataOffset, imageOffset, cis_buff);

		cisFullBufferState[line] = CIS_BUFFER_OFFSET_NONE;
	}
}

/**
 * @brief  Manages Image process.
 * @param
 * @retval None
 *
 * ---------------------------------------------------
 *	INPUT :
 *	DMA buffer in 32bits increment
 *	o = start offset
 *	e = over scan
 *	                                        X1                                   X2                                   X3
 *		 ___________________________________V ___________________________________V ___________________________________V
 *		|                DMA1               ||                DMA2               ||                DMA3               |
 * 		|       HALF      |       FULL      ||       HALF      |       FULL      ||       HALF      |       FULL      |
 * 		|     R     |     G     |     B     ||     R     |     G     |     B     ||     R     |     G     |     B     |
 * 		|o |      |e|o |      |e|o |      |e||o |      |e|o |      |e|o |      |e||o |      |e|o |      |e|o |      |e|
 * 		   ^           ^  ^        ^            ^           ^  ^        ^            ^           ^  ^        ^
 * 		   A1          B1 C1       D1           A2          B2 C2       D2           A3          B3 C3       D3
 *
 *   	A1 = CIS_START_OFFSET
 *      A2 = CIS_START_OFFSET + CIS_ADC_BUFF_SIZE
 *     	A3 = CIS_START_OFFSET + CIS_ADC_BUFF_SIZE * 2
 *
 *      B1 = CIS_LINE_SIZE + CIS_START_OFFSET
 *      B2 = CIS_LINE_SIZE + CIS_START_OFFSET + CIS_ADC_BUFF_SIZE
 *      B3 = CIS_LINE_SIZE + CIS_START_OFFSET + CIS_ADC_BUFF_SIZE * 2
 *
 *      D1 = CIS_LINE_SIZE * 2 + CIS_START_OFFSET
 *      D2 = CIS_LINE_SIZE * 2 + CIS_START_OFFSET + CIS_ADC_BUFF_SIZE
 *      D3 = CIS_LINE_SIZE * 2 + CIS_START_OFFSET + CIS_ADC_BUFF_SIZE * 2
 *
 *	DMA data conversion :
 *
 *	16bits RED :	 _______________
 *	ADC DATA => 	|   R	|	|	|
 *	16 to 8  => 	| R	|	|	|	|	>> 8
 *	                ^
 *					Ax
 *
 *	16bits GREEN : 	 _______________
 *	ADC DATA => 	|   G	|	|	|
 *	16 to 8  => 	| G	|	|	|	|	>> 8
 *	offset   => 	| 	| G	|	|	|	<< 8
 *	                ^
 *					Bx
 *
 *	16bits BLUE : 	 _______________
 *	ADC DATA => 	|   B	|	|	|
 *	16 to 8  => 	| B	|	|	|	|	>> 8
 *	offset   => 	| 	| 	| B	|	|	<< 16
 *					^
 *					Dx
 *
 * 	---------------------------------------------------
 *	OUTPUT :
 *	Data buffer in 8bits increment
 *
 *|             |                ZONE1                  ||                ZONE2                  ||                ZONE3                  |
 *	 	HALF => |RG--|RG--|RG--|RG--|R---|R---|R---|R---||RG--|RG--|RG--|RG--|R---|R---|R---|R---||RG--|RG--|RG--|RG--|R---|R---|R---|R---|
 * 		FULL => |--B-|--B-|--B-|--B-|-GB-|-GB-|-GB-|-GB-||--B-|--B-|--B-|--B-|-GB-|-GB-|-GB-|-GB-||--B-|--B-|--B-|--B-|-GB-|-GB-|-GB-|-GB-|
 * 		OUT  => |RGB-|RGB-|RGB-|RGB-|RGB-|RGB-|RGB-|RGB-||RGB-|RGB-|RGB-|RGB-|RGB-|RGB-|RGB-|RGB-||RGB-|RGB-|RGB-|RGB-|RGB-|RGB-|RGB-|RGB-|
 * 		        ^                                        ^                                        ^
 *              M1                                       M2                                       M3
 *
 */
#define CIS_GREEN_HALF_SIZE			((CIS_ADC_BUFF_SIZE / 2) - (CIS_LINE_SIZE + CIS_INACTIVE_AERA_STOP))
#define CIS_GREEN_FULL_SIZE			((CIS_PIXELS_PER_LINE) - (CIS_GREEN_HALF_SIZE))
void cis_ImageProcessRGB(int32_t *cis_buff)
{
	static int32_t dataOffset_Ax, dataOffset_Bx, dataOffset_Cx, dataOffset_Dx, imageOffset, line;

	// Read and copy DMA buffer
	for (line = CIS_ADC_OUT_LINES; --line >= 0;)
	{
		/* 1st half DMA buffer Data represent Full R region + 1/2 of G region */
		while(cisHalfBufferState[line] != CIS_BUFFER_OFFSET_HALF);

		/* Invalidate Data Cache */
		SCB_InvalidateDCache_by_Addr((uint32_t *)&cisData[CIS_ADC_BUFF_SIZE * line], (CIS_ADC_BUFF_SIZE * sizeof(uint32_t)) / 2);
		arm_copy_q31(&cisData[CIS_ADC_BUFF_SIZE * line],&cisDataCpy[CIS_ADC_BUFF_SIZE * line], CIS_ADC_BUFF_SIZE / 2);
		cisHalfBufferState[line] = CIS_BUFFER_OFFSET_NONE;

		/* 2nd full DMA buffer Data represent last 1/2 of G region + Full B region */
		while(cisFullBufferState[line] != CIS_BUFFER_OFFSET_FULL);

		/* Invalidate Data Cache */
		SCB_InvalidateDCache_by_Addr((uint32_t *)&cisData[(CIS_ADC_BUFF_SIZE * line) + (CIS_ADC_BUFF_SIZE / 2)], (CIS_ADC_BUFF_SIZE * sizeof(uint32_t)) / 2);
		arm_copy_q31(&cisData[(CIS_ADC_BUFF_SIZE * line) + (CIS_ADC_BUFF_SIZE / 2)],&cisDataCpy[(CIS_ADC_BUFF_SIZE * line) + (CIS_ADC_BUFF_SIZE / 2)], CIS_ADC_BUFF_SIZE / 2);
		cisFullBufferState[line] = CIS_BUFFER_OFFSET_NONE;
	}

//	cis_Stop_capture();

	for (line = CIS_ADC_OUT_LINES; --line >= 0;)
	{
		imageOffset = (CIS_PIXELS_PER_LINE * line);	//Mx

		dataOffset_Ax = (CIS_ADC_BUFF_SIZE * line) + CIS_START_OFFSET;									//Ax
		dataOffset_Bx = (CIS_ADC_BUFF_SIZE * line) + CIS_LINE_SIZE + CIS_START_OFFSET;					//Bx

		arm_shift_q31(&cisDataCpy[dataOffset_Ax], -8, &cisDataCpy[dataOffset_Ax], (CIS_ADC_BUFF_SIZE / 2) - CIS_START_OFFSET); 	//R + Ghalf 8 bit conversion
		arm_shift_q31(&cisDataCpy[dataOffset_Bx], 8, &cisDataCpy[dataOffset_Bx], CIS_GREEN_HALF_SIZE); 	//Ghalf 8 bit shift

		arm_add_q31(&cisDataCpy[dataOffset_Bx], &cisDataCpy[dataOffset_Ax], &cisDataCpy[dataOffset_Ax], CIS_GREEN_HALF_SIZE); //Add Green

		dataOffset_Cx = (CIS_ADC_BUFF_SIZE * line) + (CIS_ADC_BUFF_SIZE / 2);								//Cx
		dataOffset_Dx = (CIS_ADC_BUFF_SIZE * line) + (CIS_LINE_SIZE * 2) + CIS_START_OFFSET;				//Dx

		arm_shift_q31(&cisDataCpy[dataOffset_Cx], -8, &cisDataCpy[dataOffset_Cx], CIS_ADC_BUFF_SIZE / 2); 	//Gfull + B 8 bit conversion
		arm_shift_q31(&cisDataCpy[dataOffset_Cx], 8, &cisDataCpy[dataOffset_Cx], CIS_GREEN_FULL_SIZE); 		//Gfull 8 bit shift
		arm_shift_q31(&cisDataCpy[dataOffset_Dx], 16, &cisDataCpy[dataOffset_Dx], CIS_PIXELS_PER_LINE); 	//B 16 bit shift

//		arm_fill_q31(0, &cisDataCpy[dataOffset_Dx], CIS_PIXELS_PER_LINE); //Delete Blue for debug
		arm_add_q31(&cisDataCpy[dataOffset_Cx], &cisDataCpy[dataOffset_Dx + CIS_GREEN_HALF_SIZE], &cisDataCpy[dataOffset_Dx + CIS_GREEN_HALF_SIZE], CIS_GREEN_FULL_SIZE); //Add Green

		arm_add_q31(&cisDataCpy[dataOffset_Ax], &cisDataCpy[dataOffset_Dx], &cis_buff[imageOffset], CIS_PIXELS_PER_LINE); //Half + Full Data

//		arm_sub_q7(&cisDataNegate[imageOffset * 4], (int8_t *)&cis_buff[imageOffset * 4], (int8_t *)&cis_buff[imageOffset * 4], CIS_PIXELS_PER_LINE * 4);
//		arm_negate_q7((int8_t *)&cis_buff[imageOffset * 4], (int8_t *)&cis_buff[imageOffset * 4], CIS_PIXELS_PER_LINE * 4);

//		arm_fill_q31(0xFF0000 >> (line * 8), &cis_buff[imageOffset], CIS_PIXELS_PER_LINE); //RGB debug
	}
//		cis_Start_capture();
}

/**
 * @brief  Image filtering
 * @param  Audio buffer
 * @retval None
 */
void cis_ImageFilterBW(int32_t *cis_buff)
{
	for (int32_t i = 0; i < CIS_PIXELS_PER_LINE / 2; i++)
	{
#ifdef CIS_INVERT_COLOR
		cis_buff[i] = (double)(65535 - cis_buff[i]);
#endif
#ifdef CIS_INVERT_COLOR_SMOOTH
		cis_buff[i] = (double)(65535 - cis_buff[i]) * (pow(10.00, ((double)(65535 - cis_buff[i]) / 65535.00)) / 10.00); //sensibility filer generate some glitchs
#endif
#ifdef CIS_NORMAL_COLOR_SMOOTH
		cis_buff[i] = (double)(cis_buff[i]) * (pow(10.00, ((double)(cis_buff[i]) / 65535.00)) / 10.00);
#endif
	}
}

/**
 * @brief  CIS start captures
 * @param  None
 * @retval None
 */
void cis_Start_capture()
{
	/* Reset CLKs ############################################*/
	//Reset CLK counter
	__HAL_TIM_SET_COUNTER(&htim1, 0);

	//Reset SP counter
	__HAL_TIM_SET_COUNTER(&htim8, CIS_LINE_SIZE - CIS_SP_WIDTH);

#ifdef CIS_BW
	//Set BW phase shift
	__HAL_TIM_SET_COUNTER(&htim4, CIS_LINE_SIZE - CIS_LED_RED_ON);			//R
	__HAL_TIM_SET_COUNTER(&htim5, CIS_LINE_SIZE - CIS_LED_GREEN_ON);		//G
	__HAL_TIM_SET_COUNTER(&htim3, CIS_LINE_SIZE - CIS_LED_BLUE_ON);			//B
#else
	//Set RGB phase shift
	__HAL_TIM_SET_COUNTER(&htim4, (CIS_LINE_SIZE * 1) - CIS_LED_RED_ON);	//R
	__HAL_TIM_SET_COUNTER(&htim5, (CIS_LINE_SIZE * 3) - CIS_LED_GREEN_ON);	//G
	__HAL_TIM_SET_COUNTER(&htim3, (CIS_LINE_SIZE * 2) - CIS_LED_BLUE_ON);	//B
#endif

	/* Start LEDs ############################################*/
	cis_LedsOn();

	/* Start SP generation ##################################*/
	if(HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_3) != HAL_OK)
	{
		Error_Handler();
	}

	/* Start DMA #############################################*/
	if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&cisData[0], CIS_ADC_BUFF_SIZE) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)&cisData[CIS_ADC_BUFF_SIZE], CIS_ADC_BUFF_SIZE) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADC_Start_DMA(&hadc3, (uint32_t *)&cisData[CIS_ADC_BUFF_SIZE * 2], CIS_ADC_BUFF_SIZE) != HAL_OK)
	{
		Error_Handler();
	}

	/* Start CLK generation ##################################*/
	if(HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK)
	{
		Error_Handler();
	}

	/* Start ADC Timer #######################################*/
	if(HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
 * @brief  CIS stop captures
 * @param  None
 * @retval None
 */
void cis_Stop_capture()
{
	/* Start ADC Timer #######################################*/
	if(HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1) != HAL_OK)
	{
		Error_Handler();
	}

	/* Start CLK generation ##################################*/
	if(HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2) != HAL_OK)
	{
		Error_Handler();
	}

	/* Start SP generation ##################################*/
	if(HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_3) != HAL_OK)
	{
		Error_Handler();
	}

	/* Stop DMA ##############################################*/
	if (HAL_ADC_Stop_DMA(&hadc1) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADC_Stop_DMA(&hadc2) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADC_Stop_DMA(&hadc3) != HAL_OK)
	{
		Error_Handler();
	}

	/* Start LEDs ###########################################*/
	cis_LedsOff();

	cisHalfBufferState[0] = CIS_BUFFER_OFFSET_NONE;
	cisHalfBufferState[1] = CIS_BUFFER_OFFSET_NONE;
	cisHalfBufferState[2] = CIS_BUFFER_OFFSET_NONE;
	cisFullBufferState[0] = CIS_BUFFER_OFFSET_NONE;
	cisFullBufferState[1] = CIS_BUFFER_OFFSET_NONE;
	cisFullBufferState[2] = CIS_BUFFER_OFFSET_NONE;
}

/**
 * @brief  Init CIS clock Frequency
 * @param  sampling_frequency
 * @retval None
 */
void cis_TIM_CLK_Init()
{
	MX_TIM1_Init();
}

/**
 * @brief  CIS start pulse timer init
 * @param  Void
 * @retval None
 */
void cis_TIM_SP_Init()
{
	MX_TIM8_Init();
}

/**
 * @brief  CIS red led timer init
 * @param  Void
 * @retval None
 */
void cis_TIM_LED_B_Init()
{
	MX_TIM3_Init();
}

/**
 * @brief  CIS green led timer init
 * @param  Void
 * @retval None
 */
void cis_TIM_LED_R_Init()
{
	MX_TIM4_Init();
}

/**
 * @brief  CIS blue led timer init
 * @param  Void
 * @retval None
 */
void cis_TIM_LED_G_Init()
{
	MX_TIM5_Init();
}

/**
 * @brief  CIS leds off
 * @param  Void
 * @retval None
 */
void cis_LedsOff()
{
	/* Start LED R generation ###############################*/
	if(HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_2) != HAL_OK)
	{
		Error_Handler();
	}
	/* Start LED G generation ###############################*/
	if(HAL_TIM_PWM_Stop(&htim5, TIM_CHANNEL_3) != HAL_OK)
	{
		Error_Handler();
	}
	/* Start LED B generation ###############################*/
	if(HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
 * @brief  CIS leds on
 * @param  Void
 * @retval None
 */
void cis_LedsOn()
{
	/* Start LED R generation ###############################*/
	if(HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2) != HAL_OK)
	{
		Error_Handler();
	}
	/* Start LED G generation ###############################*/
	if(HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3) != HAL_OK)
	{
		Error_Handler();
	}
	/* Start LED B generation ###############################*/
	if(HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
 * @brief  CIS adc init
 * @param  Void
 * @retval None
 */
void cis_ADC_Init(void)
{
	MX_ADC1_Init();
	MX_ADC2_Init();
	MX_ADC3_Init();

	/* ### Start calibration ############################################ */
	if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADCEx_LinearCalibration_FactorLoad(&hadc1) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADCEx_LinearCalibration_FactorLoad(&hadc2) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_ADCEx_LinearCalibration_FactorLoad(&hadc3) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
 * @brief  Conversion DMA half-transfer callback in non-blocking mode
 * @param  hadc: ADC handle
 * @retval None
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc)
{
	if (hadc->Instance == ADC1)
	{
		cisHalfBufferState[0] = CIS_BUFFER_OFFSET_HALF;
	}
	if (hadc->Instance == ADC2)
	{
		cisHalfBufferState[1] = CIS_BUFFER_OFFSET_HALF;
	}
	if (hadc->Instance == ADC3)
	{
		cisHalfBufferState[2] = CIS_BUFFER_OFFSET_HALF;
	}
}

/**
 * @brief  Conversion complete callback in non-blocking mode
 * @param  hadc: ADC handle
 * @retval None
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	if (hadc->Instance == ADC1)
	{
		cisFullBufferState[0] = CIS_BUFFER_OFFSET_FULL;
	}
	if (hadc->Instance == ADC2)
	{
		cisFullBufferState[1] = CIS_BUFFER_OFFSET_FULL;
	}
	if (hadc->Instance == ADC3)
	{
		cisFullBufferState[2] = CIS_BUFFER_OFFSET_FULL;
	}
}

/**
 * @brief  CIS test
 * @param  Void
 * @retval None
 */
void cis_Test(void)
{
	uint32_t cis_color = 0;
	uint32_t i = 0;

	while (1)
	{
		ssd1362_drawRect(0, DISPLAY_AERA1_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_AERA1_Y2POS, 3, false);
		ssd1362_drawRect(0, DISPLAY_AERA2_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_AERA2_Y2POS, 8, false);

		for (i = 0; i < (DISPLAY_MAX_X_LENGTH); i++)
		{
			cis_color = cis_GetBuffData((i * ((float)CIS_PIXELS_NB / (float)DISPLAY_MAX_X_LENGTH))) >> 12;
			ssd1362_drawPixel(DISPLAY_MAX_X_LENGTH - 1 - i, DISPLAY_AERA1_Y1POS + DISPLAY_AERAS1_HEIGHT - DISPLAY_INTER_AERAS_HEIGHT - (cis_color) - 1, 15, false);

			ssd1362_drawVLine(DISPLAY_MAX_X_LENGTH - 1 - i, DISPLAY_AERA2_Y1POS + 1, DISPLAY_AERAS2_HEIGHT - 2, cis_color, false);
		}
		ssd1362_drawRect(200, DISPLAY_HEAD_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_HEAD_Y2POS, 4, false);
		ssd1362_writeFullBuffer();

		HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
	}
}

/**
 * @brief  Gets the sector of a given address
 * @param  Address Address of the FLASH Memory
 * @retval The sector of a given address
 */
uint32_t GetSector(uint32_t Address)
{
	uint32_t sector = 0;

	if(((Address < ADDR_FLASH_SECTOR_1_BANK1) && (Address >= ADDR_FLASH_SECTOR_0_BANK1)) || \
			((Address < ADDR_FLASH_SECTOR_1_BANK2) && (Address >= ADDR_FLASH_SECTOR_0_BANK2)))
	{
		sector = FLASH_SECTOR_0;
	}
	else if(((Address < ADDR_FLASH_SECTOR_2_BANK1) && (Address >= ADDR_FLASH_SECTOR_1_BANK1)) || \
			((Address < ADDR_FLASH_SECTOR_2_BANK2) && (Address >= ADDR_FLASH_SECTOR_1_BANK2)))
	{
		sector = FLASH_SECTOR_1;
	}
	else if(((Address < ADDR_FLASH_SECTOR_3_BANK1) && (Address >= ADDR_FLASH_SECTOR_2_BANK1)) || \
			((Address < ADDR_FLASH_SECTOR_3_BANK2) && (Address >= ADDR_FLASH_SECTOR_2_BANK2)))
	{
		sector = FLASH_SECTOR_2;
	}
	else if(((Address < ADDR_FLASH_SECTOR_4_BANK1) && (Address >= ADDR_FLASH_SECTOR_3_BANK1)) || \
			((Address < ADDR_FLASH_SECTOR_4_BANK2) && (Address >= ADDR_FLASH_SECTOR_3_BANK2)))
	{
		sector = FLASH_SECTOR_3;
	}
	else if(((Address < ADDR_FLASH_SECTOR_5_BANK1) && (Address >= ADDR_FLASH_SECTOR_4_BANK1)) || \
			((Address < ADDR_FLASH_SECTOR_5_BANK2) && (Address >= ADDR_FLASH_SECTOR_4_BANK2)))
	{
		sector = FLASH_SECTOR_4;
	}
	else if(((Address < ADDR_FLASH_SECTOR_6_BANK1) && (Address >= ADDR_FLASH_SECTOR_5_BANK1)) || \
			((Address < ADDR_FLASH_SECTOR_6_BANK2) && (Address >= ADDR_FLASH_SECTOR_5_BANK2)))
	{
		sector = FLASH_SECTOR_5;
	}
	else if(((Address < ADDR_FLASH_SECTOR_7_BANK1) && (Address >= ADDR_FLASH_SECTOR_6_BANK1)) || \
			((Address < ADDR_FLASH_SECTOR_7_BANK2) && (Address >= ADDR_FLASH_SECTOR_6_BANK2)))
	{
		sector = FLASH_SECTOR_6;
	}
	else if(((Address < ADDR_FLASH_SECTOR_0_BANK2) && (Address >= ADDR_FLASH_SECTOR_7_BANK1)) || \
			((Address < FLASH_END_ADDR) && (Address >= ADDR_FLASH_SECTOR_7_BANK2)))
	{
		sector = FLASH_SECTOR_7;
	}
	else
	{
		sector = FLASH_SECTOR_7;
	}

	return sector;
}


/**
 * @brief  CIS calibration
 * @param  calibration iteration
 * @retval None
 */
void cis_StartCalibration(uint32_t userFlashStartAddress, uint16_t iterationNb)
{
	uint32_t pix = 0, currState = 0;
	int32_t tmpCalibrationData[CIS_PIXELS_NB] = {0};

	/*Variable used for flash procedure*/
	static FLASH_EraseInitTypeDef eraseInitStruct;
	uint32_t firstSector = 0, nbOfSectors = 0;
	uint32_t address = 0, sectorError = 0;
	__IO uint32_t memoryProgramStatus = 0;
	__IO uint32_t data32 = 0;

	//sanity check
	if (CIS_PIXELS_NB > (32000 - 1))
	{
		printf("Flash write fail\n");
		Error_Handler();
	}

	printf("------ START CALIBRATION ------\n");

	for (uint32_t sampleNb = 0; sampleNb < iterationNb; sampleNb++)
	{
		//invalidate cache to refresh data
		SCB_InvalidateDCache_by_Addr((uint32_t *)&cisData[0] , CIS_ADC_BUFF_SIZE * 4);

		for (pix = 0; pix < CIS_PIXELS_NB; pix++)
		{
			tmpCalibrationData[pix] += cis_GetBuffData(pix);
		}

		currState = (sampleNb + 1) * 100 / iterationNb;
		ssd1362_progressBar(30, 30, currState, 0xF);

		HAL_Delay(10);
	}

	for (pix = 0; pix < CIS_PIXELS_NB; pix++)
	{
		tmpCalibrationData[pix] /= iterationNb;

#ifdef PRINT_CIS_CALIBRATION
		printf("Pix = %d, Val = %d\n", (int)pix, (int)tmpCalibrationData[pix]);
#endif
	}

	//	for (pix = 0; pix < CIS_EFFECTIVE_PIXELS; pix++) //For flash write debugging
	//	{
	//		tmpCalibrationData[pix] = pix;
	//	}

	/* -2- Unlock the Flash to enable the flash control register access *************/
	HAL_FLASH_Unlock();

	/* -3- Erase the user Flash area
		    (area defined by FLASH_USER_START_ADDR and FLASH_USER_END_ADDR) ***********/

	/* Get the 1st sector to erase */
	firstSector = GetSector(userFlashStartAddress);
	nbOfSectors = 1;

	/* Fill EraseInit structure*/
	eraseInitStruct.TypeErase     = FLASH_TYPEERASE_SECTORS;
	eraseInitStruct.VoltageRange  = FLASH_VOLTAGE_RANGE_4;
	eraseInitStruct.Banks         = FLASH_BANK_1;
	eraseInitStruct.Sector        = firstSector;
	eraseInitStruct.NbSectors     = nbOfSectors;

	if (HAL_FLASHEx_Erase(&eraseInitStruct, &sectorError) != HAL_OK)
	{
		printf("Flash write fail\n");
		Error_Handler();
	}

	/* -4- Program the user Flash area word by word
		    (area defined by FLASH_USER_START_ADDR and FLASH_USER_END_ADDR) ***********/

	pix = 0;
	address = userFlashStartAddress;

	while (address < (userFlashStartAddress + CIS_PIXELS_NB * 4))
	{
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address, (uint32_t)&tmpCalibrationData[pix]) == HAL_OK)
		{
			address = address + 32; /* increment for the next Flash word*/
			pix+=8;
		}
		else
		{
			printf("Flash write fail\n");
			Error_Handler();
		}
	}

	/* -5- Lock the Flash to disable the flash control register access (recommended
		     to protect the FLASH memory against possible unwanted operation) *********/
	HAL_FLASH_Lock();

	/* -6- Check if the programmed data is OK
	      MemoryProgramStatus = 0: data programmed correctly
	      MemoryProgramStatus != 0: number of words not programmed correctly ******/
	address = userFlashStartAddress;
	memoryProgramStatus = 0x0;

	for(pix = 0; pix < CIS_PIXELS_NB; pix++)
	{
		data32 = *(uint32_t*)address;
		__DSB();
		if(data32 != tmpCalibrationData[pix])
		{
			memoryProgramStatus++;
		}
		address+=4;
	}

	/* -7- Check if there is an issue to program data*/
	if (memoryProgramStatus != 0)
	{
		printf("Flash write fail\n");
		Error_Handler();
	}

	printf("-------------------------------\n");
}

/**
 * @brief  Load CIS calibration
 * @param  None
 * @retval None
 */
void cis_LoadCalibration(uint32_t userFlashStartAddress, int32_t *cisCalData, int32_t *minPix, int32_t *maxPix)
{
	uint32_t address = 0, pix = 0;
	int32_t deltaPix = 0;
	int32_t maxpix = 0;
	int32_t minpix = 0;

	printf("------- LOAD CALIBRATION ------\n");

	address = userFlashStartAddress;

	for(pix = 0; pix < CIS_PIXELS_NB; pix++)
	{
		cisCalData[pix] = *(uint32_t*)address;
		__DSB();

		address+=4;
	}

	arm_max_q31(cisCalData, CIS_PIXELS_NB, &maxpix, NULL);
	arm_min_q31(cisCalData, CIS_PIXELS_NB, &minpix, NULL);

	deltaPix = maxpix - minpix;

	*maxPix = maxpix;
	*minPix = minpix;

	printf("Max   Pix = %d\n", (int)maxpix);
	printf("Min   Pix = %d\n", (int)minpix);
	printf("Delta Pix = %d\n", (int)deltaPix);
	printf("-------------------------------\n");

#ifdef PRINT_CIS_CALIBRATION
	for (uint32_t pix = 0; pix < CIS_EFFECTIVE_PIXELS; pix++)
	{
		printf("Pix = %d, Val = %d\n", (int)pix, (int)cisCalData[pix]);
	}
	printf("-------------------------------\n");
#endif
}

/**
 * @brief  Display calibration menu
 * @param  None
 * @retval None
 */
static void cis_CalibrationMenu(void)
{
	/* Set header description */
	buttonState[SW1] = SWITCH_RELEASED;

#ifdef CIS_BW
	ssd1362_drawRect(0, DISPLAY_HEAD_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_HEAD_Y2POS, 4, TRUE);
	ssd1362_drawString(10, DISPLAY_HEAD_Y1POS + 1, (int8_t *)"CIS BW QUALIBRATION", 0xF, 8);
	ssd1362_writeFullBuffer();
	HAL_Delay(1000);
	ssd1362_drawRect(0, DISPLAY_HEAD_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_HEAD_Y2POS, 4, TRUE);
	ssd1362_drawString(10, DISPLAY_HEAD_Y1POS + 1, (int8_t *)"PLACE CIS ON BLACK SURFACE", 0xF, 8);
	ssd1362_writeFullBuffer();
	HAL_Delay(1000);
	cis_StartCalibration(whiteCalibVirtAddVar, 500);
	ssd1362_drawRect(0, DISPLAY_HEAD_Y1POS, DISPLAY_MAX_X_LENGTH, DISPLAY_HEAD_Y2POS, 4, TRUE);
	ssd1362_drawString(10, DISPLAY_HEAD_Y1POS + 1, (int8_t *)"PLACE CIS ON WHITE SURFACE", 0xF, 8);
	ssd1362_writeFullBuffer();
	HAL_Delay(1000);
	cis_StartCalibration(blackCalibVirtAddVar, 500);
#endif
}
