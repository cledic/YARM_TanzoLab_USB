/**
 * \file
 *
 * \brief Empty user application template
 *
 */

/**
 * \mainpage User Application template doxygen documentation
 *
 * \par Empty user application template
 *
 * Bare minimum empty user application template
 *
 * \par Content
 *
 * -# Include the ASF header files (through asf.h)
 * -# Minimal main function that starts with a call to system_init()
 * -# "Insert application code here" comment
 *
 */

/*
 * Include header files for all drivers that have been imported from
 * Atmel Software Framework (ASF).
 */
/*
 * Support and FAQ: visit <a href="http://www.atmel.com/design-support/">Atmel Support</a>
 */
#include <compiler.h>
#include <board.h>
#include <conf_board.h>
#include <port.h>

#include <asf.h>
#include "conf_usb.h"
#include <extint.h>
#include <extint_callback.h>

#include <serial.h>
#include <stdio_serial.h>
#include <sam0_usart/usart_serial.h>

// From module: RTC - Real Time Counter in Count Mode (Callback APIs)
#include <rtc_count.h>
#include <rtc_count_interrupt.h>
#include <rtc_tamper.h>

#include "YARM_lib.h"
#include "BME280_lib.h"
#include "BMP180_lib.h"
#include "terminal.h"
#include "LowPower_lib.h"

#include <stdarg.h>

void cprintf( const char* format, ... );


/* ********************************************************************* */
/* La USART sulla SERCOM3 mi dava problemi con la SPI dell'accelerometro */
/* anche lei sulla SERCOM3.                                              */
#if 0
	#define CONF_STDIO_USART          SERCOM3
	#define CONF_STDIO_MUX_SETTING    USART_RX_1_TX_0_XCK_1
	#define CONF_STDIO_PINMUX_PAD0    PINMUX_PA22C_SERCOM3_PAD0
	#define CONF_STDIO_PINMUX_PAD1    PINMUX_PA23C_SERCOM3_PAD1
	#define CONF_STDIO_PINMUX_PAD2    PINMUX_UNUSED
	#define CONF_STDIO_PINMUX_PAD3    PINMUX_UNUSED
	#define CONF_STDIO_BAUDRATE       115200

	#define CONF_STDIO_PAD0_PIN PIN_PA22C_SERCOM3_PAD0
	#define CONF_STDIO_PAD1_PIN PIN_PA23C_SERCOM3_PAD1
#else
	#define CONF_STDIO_USART          SERCOM5	
	#define CONF_STDIO_MUX_SETTING    USART_RX_1_TX_0_XCK_1
	#define CONF_STDIO_PINMUX_PAD0    PINMUX_PA22D_SERCOM5_PAD0
	#define CONF_STDIO_PINMUX_PAD1    PINMUX_PA23D_SERCOM5_PAD1
	#define CONF_STDIO_PINMUX_PAD2    PINMUX_UNUSED
	#define CONF_STDIO_PINMUX_PAD3    PINMUX_UNUSED
	#define CONF_STDIO_BAUDRATE       115200

	#define CONF_STDIO_PAD0_PIN PIN_PA22D_SERCOM5_PAD0
	#define CONF_STDIO_PAD1_PIN PIN_PA23D_SERCOM5_PAD1
#endif

static struct usart_module usart_instance;
static void configure_usart(void);
/* **************************************************************** */

#define SW2_PIN                   PIN_PA02
#define SW2_ACTIVE                false
#define SW2_INACTIVE              !SW0_ACTIVE
#define SW2_EIC_PIN               PIN_PA02A_EIC_EXTINT2
#define SW2_EIC_MUX               MUX_PA02A_EIC_EXTINT2
#define SW2_EIC_PINMUX            PINMUX_PA02A_EIC_EXTINT2
#define SW2_EIC_LINE              2

#define EVENT_PIN                   PIN_PA14
#define EVENT_ACTIVE                false
#define EVENT_INACTIVE              !EVENT_ACTIVE
#define EVENT_EIC_PIN               PIN_PA14A_EIC_EXTINT14
#define EVENT_EIC_MUX               MUX_PA14A_EIC_EXTINT14
#define EVENT_EIC_PINMUX            PINMUX_PA14A_EIC_EXTINT14
#define EVENT_EIC_LINE              14

	// http://atmel.force.com/support/articles/en_US/FAQ/Printf-with-floating-point-support-in-armgcc
	// per visualizzare i float con la printf ho aggiunto
	// in Properties,Toolchain, in -> ARM/GNU Linker, Miscellaneous 
	// -lc -u _printf_float
	// nelle flags

//
void Button_ExtIntChannel(void);
void Button_ExtIntCallbacks(void);
void Button_Callback(void);

//
void Event_ExtIntChannel(void);
void Event_ExtIntCallbacks(void);
void Event_Callback(void);

//
void PrintSysError( const char*s);

//
void ChkConsoleOn( void);
volatile uint32_t ConsoleOn;

//
void rtc_overflow_callback(void);
void configure_rtc_count(void);
void configure_rtc_callbacks(void);
struct rtc_module rtc_instance;
volatile uint32_t event_rtc;

//
uint32_t TX_ToYarmMobile( void);
uint32_t RX_FromYarmMobile( void);
uint32_t Weather_PrepareTxData( void);
uint32_t TX_ToYarmMobile_PrepareTxData( void);

//
typedef union
{
	uint8_t data[32];
	struct {
		float t;
		float p;
		float h;
		float rssi;
		uint32_t SerialNumber[3];
	} wval;
} SENSOR_DATA_t;

SENSOR_DATA_t sd;
#define SENSOR_DATA_LEN	(sizeof(SENSOR_DATA_t))

uint8_t TxSequence[]={
	0x40,		// 0, Chn 1, Serv 0
	0x50,		// 1, Chn 2, Serv 0
	0x60,		// 2, Chn 3, Serv 0
	0x41,		// 3, Chn 1, Serv 1
	0x51,		// 4, Chn 2, Serv 1
	0x61,		// 5, Chn 3, Serv 1
	0x42,		// 6, Chn 1, Serv 2
	0x52,		// 7, Chn 2, Serv 2
	0x62,		// 8, Chn 3, Serv 2
};
uint8_t seq_idx;

/* receive buffer used for all transactions */
#define BUF_LENGTH	64
static uint8_t rd_buffer[BUF_LENGTH];

int32_t	buffer_len;
float t, h, p;
float x, y, z;
char msg[512];
uint8_t TxRxEvent[16];
uint8_t TxPreambleBuffer[]={0x04, 0x70, 0x8E, 0x0A, 0x55, 0x55, 0x10, 0x55, 0x56};
//
volatile uint32_t buttom_state;
volatile uint32_t event_state;
//
volatile uint32_t *ser_ptr1 = (volatile uint32_t *)0x0080A00C;
volatile uint32_t *ser_ptr2 = (volatile uint32_t *)0x0080A040;
uint8_t	serviceChannelReceived;
uint32_t rssiReceived;
uint32_t SerialNumber[4];
uint32_t counter_cycle;
uint32_t rxError;
uint32_t rxLength;
uint32_t txLength;

uint32_t txRepeat;
volatile uint32_t received_data;
//
uint32_t i, ii;
char waiting[]={'|','/','-','\\'};

//
#define WEATHER_TX_CHNL	2		// 2, Chn 3, Serv 0


int main (void)

{
	ConsoleOn=1;

	irq_initialize_vectors();
	cpu_irq_enable();

	system_init();

	udc_start();

	/* BOD33 disabled */
	SUPC->BOD33.reg &= ~SUPC_BOD33_ENABLE;

	/* VDDCORE is supplied BUCK converter */
	SUPC->VREG.bit.SEL = SUPC_VREG_SEL_BUCK_Val;
	
	configure_usart();
		
	/* Configure and enable RTC */
	configure_rtc_count();
	/* Configure and enable callback */
	configure_rtc_callbacks();
	/* Set period */
	rtc_count_set_period( &rtc_instance, 8000);

	/* Enable External Interrupt for Button0 trap */
	// Button_ExtIntChannel();
	// Button_ExtIntCallbacks();
	Event_ExtIntChannel();
	Event_ExtIntCallbacks();
	system_interrupt_enable_global();
	
	if ( 1 /*ConsoleOn*/) Term_Banner();	
	if ( 1 /*ConsoleOn*/) printf("YARM Station\r\n");	
	
	for ( i=0; i<SENSOR_DATA_LEN; i++)
		sd.data[i]=0;

	/* */
	for ( i=0; i<BUF_LENGTH; i++) {
		rd_buffer[i]=0;
	}

	/* Configure SPI and PowerUp ATA8510 */
	YARM_Init();
	delay_ms(1);
	if ( ConsoleOn) printf("YARM_Init\r\n")	;
	
	/* MCU ID */
	SerialNumber[0]=*ser_ptr1;
	SerialNumber[1]=*ser_ptr2++;
	SerialNumber[2]=*ser_ptr2++;
		
	/* Ciclo principale */
	while( 1)
	{

#if 0
		/* Sleep 
		 * standby (d) = 4uA
		 * idle    (c) = 200uA
		 * dynamic (q) = 7uA
		*/
#if 1
		//
		EnterSleepMode2();	// OK	
#else
		// 
		EnterIdleMode();
#endif
#endif	
		delay_ms( 8*1000);
		/* Evento dalla radio */
		//if ( event_rtc) {
			event_rtc=0;
			printf("Evento timer: trasmissione a YARM Remota\r\n");
			cprintf("%s", "Evento timer: trasmissione a YARM Remota\r\n");
			
			/* Invio dati alla YARM mobile */
			TX_ToYarmMobile();
			/* Ricevo dati dalla YARM Mobile */
			printf("Trasmissione avvenuta\r\nAttesa ricezione ");
			cprintf("%s", "Trasmissione avvenuta\r\nAttesa ricezione ");
			RX_FromYarmMobile();
		//}
	}

}

/*
*/
void cprintf( const char* format, ... )
{
	va_list arglist;
	
	va_start( arglist, format );
	// vprintf( format, arglist );
	vsprintf( msg, format, arglist);
	va_end( arglist );
	
	printf("USB: %s\r\n\r\n", msg);
	
	while (!udi_cdc_is_tx_ready()) {
		// Fifo full
	}
	udi_cdc_write_buf( msg, strlen(msg));
}

/** Gestione della trasmissione di un msg via radio
 *
*/
uint32_t TX_ToYarmMobile( void)
{
	float alt=0.0f;
	uint32_t i;
	
#if 1
	YARM_SetIdleMode();
	if ( ConsoleOn) PrintSysError("YARM_SetIdleMode");
	
	/* print out YARM state */
	buffer_len = YARM_GetEventBytes( &rd_buffer[0]);
	if ( ConsoleOn) {
		if ( buffer_len < 0 ) {
			printf( "Errore YARM_GetEventBytes %d\r\n", (buffer_len*-1));
			} else {
			printf( "Reg. GETEVENTBYTES: ");
			for ( i=0; i<buffer_len; i++) {
				printf( "0x%X, ", rd_buffer[i]);
			}
			printf( "\r\n");
		}
	}
	
	buffer_len = YARM_GetVersionFlash( &rd_buffer[0]);
	if ( ConsoleOn) {
		if ( buffer_len < 0 ) {
			printf( "Errore YARM_GetVersionFlash %d\r\n", (buffer_len*-1));
			} else {
			printf( "Reg. GETVERSIONFLASH: 0x%X\r\n", rd_buffer[0]);
		}
	}
	
	/* print out YARM state */
	buffer_len = YARM_GetEventBytes( &rd_buffer[0]);
	if ( ConsoleOn) {
		if ( buffer_len < 0 ) {
			printf( "Errore YARM_GetEventBytes %d\r\n", (buffer_len*-1));
			} else {
			printf( "Reg. GETEVENTBYTES: ");
			for ( i=0; i<buffer_len; i++) {
				printf( "0x%X, ", rd_buffer[i]);
			}
			printf( "\r\n");
		}
	}
#endif

	txLength = TX_ToYarmMobile_PrepareTxData();

	if (txLength==0)
		return 1;
	
	if ( ConsoleOn) {
		printf("Data to Send [%d]\r\n", txLength);
		for ( i=0; i<txLength;i++)
			printf(" 0x%0X, ", sd.data[i]);
	
		printf("\r\n");	
	}
	
	YARM_SetIdleMode(); 
	if ( ConsoleOn) PrintSysError("YARM_SetIdleMode");
	delay_ms(1);
	
	if ( ConsoleOn) {
		TERM_TEXT_GREEN;
		printf("-------- Transmission request\r\n");
		TERM_TEXT_DEFAULT;
	}
	
	YARM_SetSystemMode( YARM_RF_TXMODE, TxSequence[ WEATHER_TX_CHNL]);
	if ( ConsoleOn) PrintSysError("YARM_SetSystemMode TXMode");
	delay_ms(1);
	
	YARM_WriteTxPreamble( YARM_WriteTxPreambleBuffer_LEN, &TxPreambleBuffer[0]);
	if ( ConsoleOn) PrintSysError("YARM_WriteTxPreamble");
	
	delay_us( 100);

	YARM_WriteTxFifo( txLength, sd.data);
	if ( ConsoleOn) PrintSysError("YARM_WriteTxFifo");
	delay_ms(100);
	
	if ( ConsoleOn) printf("TXLoop Started\r\n");
		
	// Controlla EVENT_IRQ
	event_state = 0;
	while( !event_state);
	event_state = 0;
	
	//
	i = 0;
	do {
		i++;
		delay_us( 3000);
		YARM_GetEventBytes( TxRxEvent);
		// PrintSysError( "YARM_GetEventBytes TXLoop");
	} while (((TxRxEvent[1] & 0x10) == 0)&&(i <40));

	if ( ConsoleOn) {
		printf("TXLoop Ended at cycle : %d [%d ms]\r\n", i, i*3);
		printf("TX Ended on service: %d channel: %d\r\n", (TxSequence[seq_idx] & 0x07), (TxSequence[seq_idx]>>4)&0x03);
	
		/* print out YARM state */
		buffer_len = YARM_GetEventBytes( &rd_buffer[0]);
		if ( buffer_len < 0 ) {
			printf( "Error YARM_GetEventBytes %d\r\n", (buffer_len*-1));
		} else {
			printf( "Reg. GETEVENTBYTES: ");
			for ( i=0; i<buffer_len; i++) {
				printf( "0x%X, ", rd_buffer[i]);
			}
			printf( "\r\n");
		}
	}
	
	/* */
	for ( i=0; i<SENSOR_DATA_LEN; i++) {
		sd.data[i]=0;
	}
	
	delay_us( 100);
	
	YARM_SetIdleMode(); 
	if ( ConsoleOn) PrintSysError("YARM_SetIdleMode");
	delay_ms( 1);
	
	return 1;
}

uint32_t RX_FromYarmMobile( void)
{
	//
	for ( i=0; i<BUF_LENGTH; i++) {
		rd_buffer[i]=0;
	}
	
	// Programmo la YARM in polling mode x la ricezione
//	YARM_SetSystemMode( YARM_RF_RXMODE, TxSequence[ WEATHER_TX_CHNL]); //YARM_SetIdleMode();
//	PrintSysError("YARM_SetSystemMode TXMode");

	YARM_SetPollingMode(); 
	//rtc_count_set_period( &rtc_instance, 3000);
	
	// Entro in ciclo aspettando due eventi: la ricezione dall'ATA8510 o lo
	// scadere del timer.
	while( 1)
	{
		if ( event_state || event_rtc) {
			break;
		}
	}
	//
	if ( event_rtc) {
		// Non ho ricevuto nulla
		if ( ConsoleOn) {
			TERM_TEXT_RED;
			printf("ERRORE: Non ho ricevuto dati ! ! ! \r\n");
			TERM_TEXT_DEFAULT;
		}
		// carico nuovamente l'RTC a 5sec
		// rtc_count_set_period( &rtc_instance, 5000);
		event_rtc=0;
		event_state=0;
		//
		YARM_SetIdleMode();
		if ( ConsoleOn) PrintSysError("YARM_SetIdleMode");
		delay_ms( 10);

		return 1;
	} 
	// ****
	// Gestisco la ricezione dalla YARM Remota
	// Loop waiting EOTA
	if ( ConsoleOn) {
		TERM_TEXT_GREEN;
		printf("\r\n\r\n---- RX da YARM Mobile ------------------\r\n");
		TERM_TEXT_DEFAULT;
	}
	
	rxError=0;
	ii=0;
	do {
		delay_us( 500);
		//
		buffer_len = YARM_GetEventBytes( &rd_buffer[0]);
		serviceChannelReceived = rd_buffer[3];
		//PrintSysError("YARM_GetEventBytes EOTA");
	} while( (rd_buffer[1] & 0x10) == 0);			// EOTA: End of Telegram on path A
		
	delay_us( 5000);
		
	// clear data buffer
	for ( i=0; i<SENSOR_DATA_LEN; i++) {
		sd.data[i]=0;
	}
	
	// verify Rx FIFO
	buffer_len = YARM_ReadRxFillLevel( &rd_buffer[0]);
	i=rd_buffer[0];
	// check FIFO length
	if ( i>0 && i <= 32) {
		YARM_ReadRxFifo( i, rd_buffer);
		// Checksum test
		if (rd_buffer[i-1+3] != checksum(rd_buffer+3, i-1)) {
			rxError|=1;
		}
	}
	
	//
	TERM_TEXT_GREEN;
	printf("Ricevuti %d bytes [%d]\r\n", i, rxError);
	TERM_TEXT_DEFAULT;
	
	// copy byte data on sensors data structure
	for ( ii=0; ii<SENSOR_DATA_LEN; ii++) {
		sd.data[ii]=rd_buffer[ii+3];
	}
	
	buffer_len = YARM_ReadRssiFillLevel( &rd_buffer[0]);
		
	rssiReceived=0;
	i=rd_buffer[0];
	
	if ( i!=0) {
		YARM_ReadRssiFifo( i, rd_buffer);
		ii=0;
		while( ii<i) {
			rssiReceived += rd_buffer[ii+3];
			ii++;
		}
		//
		rssiReceived/=i;
	} else {
		rxError |= 2;
	}
	
	// prepare the JSON string
	TERM_TEXT_GREEN;
	printf("remoteID: 0x%04X%04X%04X\r\n",sd.wval.SerialNumber[0],
												sd.wval.SerialNumber[1],
												sd.wval.SerialNumber[2]);
	printf("temperature: %.2f\r\n",sd.wval.t);
	printf("pressure: %.2f\r\n", (sd.wval.p/100.0f));
	printf("humidity: %.2f\r\n", sd.wval.h);
	printf("RSSI Remote: %.2f dBm\r\n", sd.wval.rssi);
	printf("RSSI: %.2f dBm\r\n", (rssiReceived*1.0f/2.0f)-134.0f);
	printf("RxError: %d\r\n", rxError);	
	printf("---- RX da YARM Mobile ------------------\r\n");
	TERM_TEXT_DEFAULT;
	printf("\r\n\r\n");
	
	return 0;
}

uint32_t TX_ToYarmMobile_PrepareTxData( void)
{
	uint32_t c;
	
	for ( c=0; c<SENSOR_DATA_LEN; c++) {
		sd.data[c]=0;
	}
	
	sd.wval.SerialNumber[0]=SerialNumber[0];
	sd.wval.SerialNumber[1]=SerialNumber[1];
	sd.wval.SerialNumber[2]=SerialNumber[2];

	//
	sd.data[31]=checksum( &sd.data[0], 31);
	
	return 32;

}

uint32_t Weather_PrepareTxData( void)
{
	uint32_t c;
	
	for ( c=0; c<SENSOR_DATA_LEN; c++) {
		sd.data[c]=0;
	}
	
	BME280_Get_AllValues( &p, &t, &h);
	if ( ConsoleOn) {
		TERM_TEXT_GREEN;
		printf("Temp:%f, Press: %f, Hum: %f\r\n", t, p, h);
		TERM_TEXT_DEFAULT;
	}
	
	sd.wval.rssi=0.0f;
	sd.wval.p = p;
	sd.wval.t = t;
	sd.wval.h = 50.0f;
	/* MCU ID */
	sd.wval.SerialNumber[0]=SerialNumber[0];
	sd.wval.SerialNumber[1]=SerialNumber[1];
	sd.wval.SerialNumber[2]=SerialNumber[2];

	//
	sd.data[31]=checksum( &sd.data[0], 31);
	
	return 32;
}

void rtc_overflow_callback(void)
{
	/* Do something on RTC overflow here */
	event_rtc=1;
}

void configure_rtc_count(void)
{
	struct rtc_count_config config_rtc_count;
	rtc_count_get_config_defaults(&config_rtc_count);
	config_rtc_count.prescaler           = RTC_COUNT_PRESCALER_DIV_1;
	config_rtc_count.mode                = RTC_COUNT_MODE_16BIT;
	//	#ifdef FEATURE_RTC_CONTINUOUSLY_UPDATED
	//	config_rtc_count.continuously_update = true;
	//	#endif
	rtc_count_init(&rtc_instance, RTC, &config_rtc_count);
	rtc_count_enable(&rtc_instance);
}

void configure_rtc_callbacks(void)
{
	rtc_count_register_callback( &rtc_instance, rtc_overflow_callback, RTC_COUNT_CALLBACK_OVERFLOW);
	rtc_count_enable_callback( &rtc_instance, RTC_COUNT_CALLBACK_OVERFLOW);
	event_rtc=0;
}

void Event_ExtIntChannel(void)
{
	struct extint_chan_conf config_extint_chan;
	extint_chan_get_config_defaults( &config_extint_chan);

	config_extint_chan.gpio_pin           = EVENT_EIC_PIN;
	config_extint_chan.gpio_pin_mux       = EVENT_EIC_MUX;
	config_extint_chan.gpio_pin_pull      = EXTINT_PULL_UP;
	config_extint_chan.detection_criteria = EXTINT_DETECT_FALLING;
	extint_chan_set_config( EVENT_EIC_LINE, &config_extint_chan);
}

void Event_ExtIntCallbacks(void)
{
	extint_register_callback( Event_Callback, EVENT_EIC_LINE, EXTINT_CALLBACK_TYPE_DETECT);
	extint_chan_enable_callback( EVENT_EIC_LINE, EXTINT_CALLBACK_TYPE_DETECT);
}

void Event_Callback(void)
{
	event_state = 1;
	// event_state = port_pin_get_input_level( EVENT_PIN);
	// port_pin_set_output_level(LED_0_PIN, pin_state);
}


void Button_ExtIntChannel(void)
{
	struct extint_chan_conf config_extint_chan;
	extint_chan_get_config_defaults( &config_extint_chan);

	config_extint_chan.gpio_pin           = SW2_EIC_PIN;
	config_extint_chan.gpio_pin_mux       = SW2_EIC_MUX;
	config_extint_chan.gpio_pin_pull      = EXTINT_PULL_UP;
	config_extint_chan.detection_criteria = EXTINT_DETECT_FALLING;
	extint_chan_set_config( SW2_EIC_LINE, &config_extint_chan);
}

void Button_ExtIntCallbacks(void)
{
	extint_register_callback( Button_Callback, SW2_EIC_LINE, EXTINT_CALLBACK_TYPE_DETECT);
	extint_chan_enable_callback( SW2_EIC_LINE, EXTINT_CALLBACK_TYPE_DETECT);
}

void Button_Callback(void)
{
	buttom_state = 1;
	// buttom_state = port_pin_get_input_level(BUTTON_0_PIN);
	// port_pin_set_output_level(LED_0_PIN, pin_state);
}

void PrintSysError( const char*s)
{
	//TERM_CURSOR_SAVE;
	if ( YARM_GetSysError())
	{
		//
		YARM_ReadSram( 1, 0x03, 0x00, TxRxEvent);
		TERM_BKGRD_WHITE;
		TERM_TEXT_RED;
		//TERM_CURSOR_POS(1,1);
		printf("Error at %s, ErrorCode: %d\r\n", s, TxRxEvent[5]);
	} else {
		TERM_TEXT_DEFAULT;
		//TERM_CURSOR_POS(1,1);
		printf("Done %s [0x%X 0x%X]\r\n", s, YARM_Events[0], YARM_Events[1]);
	}
	TERM_TEXT_DEFAULT;
	//TERM_CURSOR_RESTORE;
}

/**
 * \brief Configure usart.
 */
void configure_usart(void)
{
	struct usart_config config_usart;
	
	usart_get_config_defaults(&config_usart);
	config_usart.baudrate    = CONF_STDIO_BAUDRATE;
	config_usart.mux_setting = CONF_STDIO_MUX_SETTING;
	config_usart.pinmux_pad0 = CONF_STDIO_PINMUX_PAD0;
	config_usart.pinmux_pad1 = CONF_STDIO_PINMUX_PAD1;
	config_usart.pinmux_pad2 = CONF_STDIO_PINMUX_PAD2;
	config_usart.pinmux_pad3 = CONF_STDIO_PINMUX_PAD3;
	
	while (usart_init(&usart_instance, CONF_STDIO_USART, &config_usart) != STATUS_OK); 
	stdio_serial_init(&usart_instance, CONF_STDIO_USART, &config_usart);

	usart_enable(&usart_instance);
}

/*
*/
void ChkConsoleOn( void)
{
	struct port_config pin_conf;
	
	/* PA11 as ConsoleEnable */
	pin_conf.direction  = PORT_PIN_DIR_INPUT;
	pin_conf.input_pull = PORT_PIN_PULL_UP;
	port_pin_set_config(PIN_PA11, &pin_conf);
	
	ConsoleOn = !(port_pin_get_input_level(PIN_PA11));
}
