#include <FreeRTOS.h>
#include <AT91SAM7.h>
#include <board.h>
#include <beacontypes.h>

#include <semphr.h>
#include <task.h>

#include <stdio.h>
#include <stdint.h>

#include "led.h"
#include "pio_irq.h"
#include "eink/eink.h"
#include "eink/eink_lowlevel.h"

/* From the example code */
const struct eink_display_configuration EINK_DISPLAY_CONFIGURATIONS[] = {
		[EINK_DISPLAY_60] = {
				.hsize = 800, .vsize = 600,
				.fslen = 4,  .fblen = 4,  .felen = 10,
				.lslen = 10, .lblen = 4,  .lelen = 74,
				.pixclkdiv = 6,
				.sdrv_cfg = ( 100 | ( 1 << 8 ) | ( 1 << 9 )  ),
				.gdrv_cfg = 0x2,
				.lutidxfmt = ( 4 | ( 1 << 7 ) )
		},
		[EINK_DISPLAY_80] = {
				.hsize = 1024, .vsize = 768,
				.fslen = 0,  .fblen = 4,  .felen = 80,
				.lslen = 10, .lblen = 20, .lelen = 80,
				.pixclkdiv = 3,
				.sdrv_cfg = ( 100 | ( 0 << 8 ) | ( 1 << 9 ) ),
				.gdrv_cfg = 0x2,
				.lutidxfmt = ( 4 | ( 1 << 7 ) )
		},
		[EINK_DISPLAY_97] = {
				.hsize = 1200, .vsize = 825,
				.fslen = 0,  .fblen = 4,  .felen = 4,
				.lslen = 4,  .lblen = 10, .lelen = 60,
				.pixclkdiv = 3,
				.sdrv_cfg = ( 100 | ( 1 << 8 ) | ( 1 << 9 ) ),
				.gdrv_cfg = 0x2,
				.lutidxfmt = ( 4 | ( 1 << 7 ) )
		},
};

#define EINK_CURRENT_DISPLAY_TYPE EINK_DISPLAY_97
const struct eink_display_configuration *EINK_CURRENT_DISPLAY_CONFIGURATION = &(EINK_DISPLAY_CONFIGURATIONS[EINK_CURRENT_DISPLAY_TYPE]);

#define EINK_READY() AT91F_PIO_IsInputSet(FPC_NHRDY_PIO, FPC_NHRDY_PIN)
/* Note that the address bits are not decoded */ 
static volatile u_int16_t *eink_base = (volatile u_int16_t*)0x10000000; 

#define EINK_BEGIN_COMMAND() AT91F_PIO_ClearOutput(FPC_HDC_PIO, FPC_HDC_PIN);
#define EINK_END_COMMAND() AT91F_PIO_SetOutput(FPC_HDC_PIO, FPC_HDC_PIN);

static xSemaphoreHandle eink_irq_semaphore;
static portBASE_TYPE eink_irq(AT91PS_PIO pio, u_int32_t pin, portBASE_TYPE xTaskWoken)
{
	(void)pio; (void)pin;
	/* Wake up the main thread, then disable IRQ */
	xSemaphoreGiveFromISR(eink_irq_semaphore, &xTaskWoken);
	pio_irq_disable(FPC_HIRQ_PIO, FPC_HIRQ_PIN);
	return xTaskWoken;
}

static void __attribute__((unused)) eink_wait_for_irq(void)
{
	pio_irq_enable(FPC_HIRQ_PIO, FPC_HIRQ_PIN);
	xSemaphoreTake(eink_irq_semaphore, portMAX_DELAY);
}

/* in eink_mgmt.c, not public */
extern int eink_mgmt_flush_queue(void);

xSemaphoreHandle eink_memory_access_mutex;
xSemaphoreHandle eink_general_access_mutex;
xSemaphoreHandle eink_usage_counter_access_mutex;
static int eink_powersave_active = 0;
static int eink_usage_counter = 0;
static long eink_usage_counter_zero_time = 0;
static int eink_was_initialized = 0;

static int _eink_perform_command_unchecked(const u_int16_t command, const u_int16_t * const params, const size_t params_len, u_int16_t * const response, const size_t response_len);
static void eink_wait_for_completion(void);

/* Send controller to Standby (PLL active, RAM active) */
static void eink_sleep(void)
{
	_eink_perform_command_unchecked(EINK_CMD_STBY, 0, 0, 0, 0);
	eink_wait_for_completion();
	//led_set_green(0);
	eink_powersave_active = 1;
}

/* Wake up controller and send it to Run */
static void eink_wakeup(void)
{
	_eink_perform_command_unchecked(EINK_CMD_RUN_SYS, 0, 0, 0, 0);
	eink_wait_for_completion();
	eink_powersave_active = 0;
	//led_set_green(1);
}

/* Increment usage counter, maybe wake up controller */
void eink_begin_use(void)
{
	xSemaphoreTake(eink_usage_counter_access_mutex, portMAX_DELAY);
	if(eink_powersave_active) eink_wakeup();
	eink_usage_counter++;
	xSemaphoreGive(eink_usage_counter_access_mutex);
}

/* Decrement usage counter, eink_main_task will initiate standby when the usage counter was 0 for EINK_SLEEP_DELAY ms. */
void eink_end_use(void)
{
	xSemaphoreTake(eink_usage_counter_access_mutex, portMAX_DELAY);
	eink_usage_counter--;
	if(eink_usage_counter <= 0) {
		eink_usage_counter = 0;
		eink_usage_counter_zero_time = xTaskGetTickCount();
	}
	xSemaphoreGive(eink_usage_counter_access_mutex);
}

static void eink_main_task(void *parameter)
{
	(void)parameter;
	while(1) {
#if 0
		eink_wait_for_irq();
		printf("eInk IRQ\n");
#endif
		xSemaphoreTake(eink_memory_access_mutex, portMAX_DELAY);
		if(eink_mgmt_flush_queue()) {
			/* Display contents were modified, wait before allowing to start another memory transfer */
			led_set_red(1);
			while(eink_read_register(0x338) & (1L<<2)) vTaskDelay(3);
			led_set_red(0);
		}
		
		xSemaphoreTake(eink_usage_counter_access_mutex, portMAX_DELAY);
		if(eink_was_initialized
				&& !eink_powersave_active
				&& eink_usage_counter == 0 
				&& (xTaskGetTickCount() - eink_usage_counter_zero_time) >= (EINK_SLEEP_DELAY/portTICK_RATE_MS)
				&& eink_job_count_pending() == 0) {
			/* Initiate sleep only when the usage counter was zero for some time *AND*
			 * No jobs are pending. This guarantees that no current display activity 
			 * is taking place (job_count_pending() only returns zero after all
			 * scheduled LUTs have finished operating and the corresponding jobs have
			 * been reaped), if only the eink_mgmt interface is used to perform
			 * display operations. If the display is activated directly through the low-level
			 * interface this will fail.
			 * FIXME: All other eInk operations (e.g. flash) must be augmented to correctly
			 * maintain the usage counter as well.
			 */
			eink_sleep();
		}
		xSemaphoreGive(eink_usage_counter_access_mutex);
		
		xSemaphoreGive(eink_memory_access_mutex);
		vTaskDelay(10);
	}
}

static void eink_wait_for_completion(void) 
{
	while(!EINK_READY()) ;
}

void eink_wait_display_idle(void)
{
	eink_perform_command(EINK_CMD_WAIT_DSPE_TRG, 0, 0, 0, 0);
	eink_perform_command(EINK_CMD_WAIT_DSPE_FREND, 0, 0, 0, 0);
	eink_wait_for_completion();
}

/* Call eink_wait_display_idle() afterwards, if necessary */
void eink_display_update_full(u_int16_t mode)
{
	u_int16_t tmp[] = { mode };
	eink_perform_command(EINK_CMD_UPD_FULL, tmp, 1, 0, 0);
}

/* Call eink_wait_display_idle() afterwards, if necessary */
void eink_display_update_full_area(u_int16_t mode, u_int16_t x, u_int16_t y, u_int16_t width, u_int16_t height)
{
	u_int16_t tmp[] = { mode,  x, y, width, height};
	eink_perform_command(EINK_CMD_UPD_FULL_AREA, tmp, 5, 0, 0);
}

/* Call eink_wait_display_idle() afterwards, if necessary */
void eink_display_update_part(u_int16_t mode)
{
	u_int16_t tmp[] = { mode };
	eink_perform_command(EINK_CMD_UPD_PART, tmp, 1, 0, 0);
}

/* Call eink_wait_display_idle() afterwards, if necessary */
void eink_display_update_part_area(u_int16_t mode, u_int16_t x, u_int16_t y, u_int16_t width, u_int16_t height)
{
	u_int16_t tmp[] = { mode,  x, y, width, height};
	eink_perform_command(EINK_CMD_UPD_PART_AREA, tmp, 5, 0, 0);
}

static u_int16_t streamed_checksum;
static u_int16_t received_checksum;
static void eink_burst_write_begin(void)
{
	eink_begin_use();

	/* Always take the semaphores in the same order! First memory_access, then general access
	 * Do not release general access until the download is complete, otherwise other threads
	 * might get to perform register operations and upset our CMD_WR_REG. 
	 */
	xSemaphoreTake(eink_memory_access_mutex, portMAX_DELAY);
	
	/* Zero the checksum register */
	eink_write_register(0x156, 0);
	streamed_checksum = 0;
	
	/* Burst Write, basically the same as writing the whole image to register 0x154, e.g. one
	 * long write_register */
	xSemaphoreTake(eink_general_access_mutex, portMAX_DELAY);
	
	EINK_BEGIN_COMMAND();
	eink_base[0] = EINK_CMD_WR_REG;
	EINK_END_COMMAND();
	eink_base[1] = 0x154;
	eink_wait_for_completion();
}

#define ADD_16(checksum, item) \
	"ADD %[" checksum "], %[" checksum "], %[" item "]\n\t" /* checksum += item */ \
	"ADD %[" checksum "], %[" checksum "], %[" item "], LSR #16\n\t" /* checksum += item >> 16 */

static void _eink_burst_write_with_checksum_40(const unsigned char * const data, unsigned int length)
{
	uint32_t checksum = streamed_checksum;
	const void *sendbuf = data;
	register unsigned int l asm("r10") = length;
	while(l--) {
		register uint32_t item1 asm("r0"),
			item2 asm("r1"),
			item3 asm("r2"),
			item4 asm("r3"),
			item5 asm("r4"),
			item6 asm("r5"),
			item7 asm("r6"),
			item8 asm("r7"),
			item9 asm("r8"),
			item10 asm("r9");
		
		asm volatile(
				"LDM %[sendbuf]!, {%[item1]-%[item10]}\n\t" /* {item1, ..., item10} = *sendbuf++ */
				"STM %[eink_base], {%[item1]-%[item10]}\n\t"

#if 0 /* ARM6 core (not on the AT91SAM7SE */
				"ADD16 %[item1], %[item1], %[item2]\n\t"
				"ADD16 %[item1], %[item1], %[item3]\n\t"
				"ADD16 %[item1], %[item1], %[item4]\n\t"
				"ADD16 %[item1], %[item1], %[item5]\n\t"
				"ADD16 %[item1], %[item1], %[item6]\n\t"
				"ADD16 %[item1], %[item1], %[item7]\n\t"
				"ADD16 %[item1], %[item1], %[item8]\n\t"
				"ADD16 %[item1], %[item1], %[item9]\n\t"
				"ADD16 %[item1], %[item1], %[item10]\n\t"
				
				"ADD %[checksum], %[checksum], %[item1]\n\t"
				"ADD %[checksum], %[checksum], %[item1], LSR #16\n\t"
#else
				ADD_16("checksum", "item1")
				ADD_16("checksum", "item2")
				ADD_16("checksum", "item3")
				ADD_16("checksum", "item4")
				ADD_16("checksum", "item5")
				ADD_16("checksum", "item6")
				ADD_16("checksum", "item7")
				ADD_16("checksum", "item8")
				ADD_16("checksum", "item9")
				ADD_16("checksum", "item10")
#endif
				
				: [checksum] "+r" (checksum), 
					[item1] "=r" (item1), [item2] "=r" (item2), 
					[item3] "=r" (item3), [item4] "=r" (item4), 
					[item5] "=r" (item5), [item6] "=r" (item6), 
					[item7] "=r" (item7), [item8] "=r" (item8), 
					[item9] "=r" (item9), [item10] "=r" (item10), 
					[sendbuf] "+r" (sendbuf) 
				: [eink_base] "r" (eink_base)
			);
	}
	streamed_checksum = checksum;
}


static void _eink_burst_write_with_checksum_2(const unsigned char * const data, unsigned int length)
{
	unsigned int i;
	const u_int16_t *sendbuf = (const u_int16_t*)data;
	for(i=0; i<length; i++) {
		streamed_checksum += *sendbuf;
		eink_base[0] = *(sendbuf++);
	}
}

static void eink_burst_write_with_checksum(const unsigned char * data, unsigned int length)
{
	/* Guard that data must be 16-bit aligned, length a multiple of 16 bits and length at least one 16-bit transfer */
	if(length < 2 || length % 2 != 0 || ((unsigned int)data)%2 != 0 ) return;
	
	eink_begin_use();
	
	/* Call the unoptimised transfer until the start is 32-bit aligned, then the optimised transfer for
	 * a multiple of 40 bytes, then the unoptimised again for the remainder of the buffer.
	 */
	while( ((unsigned int)data)%4 != 0 && length > 0 ) {
		_eink_burst_write_with_checksum_2(data, 1);
		data += 2; length -= 2;
	}
	
	_eink_burst_write_with_checksum_40(data, length/40);
	data += 40*(length/40); length -= 40*(length/40);
	
	if(length > 0) {
		_eink_burst_write_with_checksum_2(data, length/2);
	}
	
	eink_end_use();
}

static void eink_burst_write_end(void)
{
	/* This ends the burst write, allowing other register operations. Reenable
	 * memory operations only after the checksum register has been read.
	 */
	xSemaphoreGive(eink_general_access_mutex);
	received_checksum = eink_read_register(0x156);
	xSemaphoreGive(eink_memory_access_mutex);
	
	eink_end_use();
}

static int eink_burst_write_check_checksum(void)
{
	return streamed_checksum == received_checksum;
}

int eink_display_raw_image(const unsigned char *data, unsigned int length)
{
	
	eink_perform_command(EINK_CMD_BST_END_SDR, 0, 0, 0, 0);
	u_int16_t params[] = {EINK_IMAGE_BUFFER_START & 0xFFFF, EINK_IMAGE_BUFFER_START >> 16, 
			(length/2) & 0xFFFF, (length/2) >> 16};
	eink_perform_command(EINK_CMD_BST_WR_SDR, params, 4, 0, 0);
	eink_wait_for_completion();
	
	eink_burst_write_begin();
	
	eink_burst_write_with_checksum(data, length);
	
	eink_burst_write_end();
	
	eink_perform_command(EINK_CMD_BST_END_SDR, 0, 0, 0, 0);
	eink_wait_for_completion();
	
	return eink_burst_write_check_checksum();
}

void eink_init_rotmode(u_int16_t rotation_mode)
{
	const u_int16_t rotmode[] = { rotation_mode };
	eink_perform_command(EINK_CMD_INIT_ROTMODE, rotmode, 1, 0, 0);
}

void eink_set_load_address(u_int32_t load_address)
{
	u_int16_t buf[] = { load_address & 0xFFFF, (load_address >> 16) & 0xFFFF};
	eink_perform_command(EINK_CMD_LD_IMG_SETADR, buf, 2, 0, 0);
	eink_wait_for_completion();
}

void eink_set_display_address(u_int32_t display_address)
{
	u_int16_t buf[] = { display_address & 0xFFFF, (display_address >> 16) & 0xFFFF};
	eink_perform_command(EINK_CMD_UPD_SET_IMGADR, buf, 2, 0, 0);
	eink_wait_for_completion();
}

int eink_display_packed_image(u_int16_t packmode, const unsigned char *data, unsigned int length)
{
	eink_display_streamed_image_begin(packmode);
	eink_display_streamed_image_update(data, length);
	return eink_display_streamed_image_end();
}

void eink_display_streamed_area_begin(u_int16_t packmode, u_int16_t xstart, u_int16_t ystart, 
		u_int16_t width, u_int16_t height)
{
	u_int16_t buf[] = {packmode, xstart, ystart, width, height};
	eink_perform_command(EINK_CMD_LD_IMG_AREA, buf, 5, 0, 0);
	eink_wait_for_completion();
	
	eink_burst_write_begin();
}

void eink_display_streamed_image_begin(u_int16_t packmode)
{
	eink_perform_command(EINK_CMD_LD_IMG, &packmode, 1, 0, 0);
	eink_wait_for_completion();
	
	eink_burst_write_begin();
}

void eink_display_streamed_image_update(const unsigned char *data, unsigned int length)
{
	eink_burst_write_with_checksum(data, length);
}

int eink_display_streamed_image_end(void)
{
	eink_burst_write_end();
	
	eink_perform_command(EINK_CMD_LD_IMG_END, 0, 0, 0, 0);
	eink_wait_for_completion();
	
	return eink_burst_write_check_checksum();
}

#if 0
void eink_dump_raw_image(void)
{
	unsigned int length = 100;
	u_int16_t tmp[length/2];
	eink_perform_command(EINK_CMD_BST_END_SDR, 0, 0, 0, 0);
	u_int16_t params[] = {EINK_IMAGE_BUFFER_START & 0xFFFF, EINK_IMAGE_BUFFER_START >> 16, 
			(length/2) & 0xFFFF, (length/2) >> 16};
	eink_perform_command(EINK_CMD_BST_RD_SDR, params, 4, 0, 0);
	eink_wait_for_completion();
	
	/* Burst Write, basically the same as reading the whole image from register 0x154 */
	EINK_BEGIN_COMMAND();
	eink_base[0] = EINK_CMD_RD_REG;
	EINK_END_COMMAND();
	eink_base[1] = 0x154;
	eink_wait_for_completion();
	
	unsigned int i;
	for(i=0; i<length/2; i++) {
		tmp[i] = eink_base[0];
	}
	
	eink_perform_command(EINK_CMD_BST_END_SDR, 0, 0, 0, 0);
	eink_wait_for_completion();
	
	for(i=0; i<length/2; i++) {
		printf("%04X ", tmp[i]);
	}
	printf("\n");
	
	eink_write_register(0x140, 1L<<15); /* Reset host memory interface (recommended in spec after read) */
	eink_wait_for_completion();
}
#endif

u_int16_t eink_read_register(const u_int16_t reg)
{
	eink_begin_use();
	
	xSemaphoreTake(eink_general_access_mutex, portMAX_DELAY);
	EINK_BEGIN_COMMAND();
	eink_base[0] = EINK_CMD_RD_REG;
	EINK_END_COMMAND();
	eink_base[1] = reg;
	uint16_t result = eink_base[2];
	xSemaphoreGive(eink_general_access_mutex);

	eink_end_use();
	
	return result;
}

void eink_write_register(const u_int16_t reg, const u_int16_t value)
{
	eink_begin_use();
	
	xSemaphoreTake(eink_general_access_mutex, portMAX_DELAY);
	EINK_BEGIN_COMMAND();
	eink_base[0] = EINK_CMD_WR_REG;
	EINK_END_COMMAND();
	eink_base[1] = reg;
	eink_base[2] = value;
	xSemaphoreGive(eink_general_access_mutex);
	
	eink_end_use();
}

/* Perform a command without checking the usage counter.
 * DO NOT USE THIS FUNCTION OUTSIDE OF THE SLEEP CODE!
 */
static int _eink_perform_command_unchecked(const u_int16_t command, 
		const u_int16_t * const params, const size_t params_len,
		u_int16_t * const response, const size_t response_len)
{
	unsigned int i;
	eink_wait_for_completion();
	xSemaphoreTake(eink_general_access_mutex, portMAX_DELAY);
	EINK_BEGIN_COMMAND();
	eink_base[0] = command;
	EINK_END_COMMAND();
	for(i = 0; i<params_len; i++) eink_base[i] = params[i];
	for(i = 0; i<response_len; i++) response[i] = eink_base[i];
	xSemaphoreGive(eink_general_access_mutex);
	return 0;
}

int eink_perform_command(const u_int16_t command, 
		const u_int16_t * const params, const size_t params_len,
		u_int16_t * const response, const size_t response_len)
{
	eink_begin_use();
	
	_eink_perform_command_unchecked(command, params, params_len, response, response_len);
	
	eink_end_use();
	return 0;
}


void eink_controller_reset(void)
{
	volatile int i;
	AT91F_PIO_ClearOutput(FPC_NRESET_PIO, FPC_NRESET_PIN);
	for(i=0; i<192000; i++) ;
	AT91F_PIO_SetOutput(FPC_NRESET_PIO, FPC_NRESET_PIN);
}

int eink_comm_test(u_int16_t reg1, u_int16_t reg2)
{
	/* Check the data bus: Write something to two registers, 
	 * read it back and verify that it's correct. (Also: make a backup of the old value
	 * and restore it afterwards.) */
	u_int16_t backup1 = eink_read_register(reg1),
		backup2 = eink_read_register(reg2);
	const u_int16_t test1 = 0x0123;
	const u_int16_t test2 = 0x4567;
	
	eink_write_register(reg1, test1);
	eink_write_register(reg2, test2);
	eink_read_register(reg1); /* Dummy read, fixme */
	if(eink_read_register(reg1) != test1) 
		return 0;
	if(eink_read_register(reg2) != test2) 
		return 0;
	
	eink_write_register(reg1, ~test2);
	eink_write_register(reg2, ~test1);
	if(eink_read_register(reg1) != (u_int16_t)~test2) 
		return 0;
	if(eink_read_register(reg2) != (u_int16_t)~test1) 
		return 0;
	
	eink_write_register(reg1, backup1);
	eink_write_register(reg2, backup2);
	return 1;
}

int eink_controller_check_supported(void)
{
	u_int16_t product = eink_read_register(2);
	if(product != 0x0047) return -EINK_ERROR_NOT_DETECTED;
	
	u_int16_t revision = eink_read_register(0);
	if(revision != 0x0100) return -EINK_ERROR_NOT_SUPPORTED;
	
	return 0;
}

int eink_controller_init(void)
{
	eink_begin_use();
	int result = eink_controller_check_supported();
	if(result != 0) return result;
	
	eink_perform_command(EINK_CMD_INIT_SYS_RUN, 0, 0, 0, 0);
	eink_wait_for_completion();
	
	/* Perform comm test, use host memory count and checksum registers as scratch space */
	if(!eink_comm_test(0x148, 0x156)) {
		eink_end_use();
		return -EINK_ERROR_COMMUNICATIONS_FAILURE;
	}
	/* Now reset host memory interface */
	eink_write_register(0x140, 1L<<15);

	/* the following three commands are from the example code */
	eink_write_register(0x106, 0x203);
	
	const u_int16_t dspe_cfg[] = {
			EINK_CURRENT_DISPLAY_CONFIGURATION->hsize, 
			EINK_CURRENT_DISPLAY_CONFIGURATION->vsize,
			EINK_CURRENT_DISPLAY_CONFIGURATION->sdrv_cfg,
			EINK_CURRENT_DISPLAY_CONFIGURATION->gdrv_cfg,
			EINK_CURRENT_DISPLAY_CONFIGURATION->lutidxfmt,
	};
	eink_perform_command(EINK_CMD_INIT_DSPE_CFG, dspe_cfg, 5, 0, 0);
	
	const u_int16_t dspe_tmg[] = {
			EINK_CURRENT_DISPLAY_CONFIGURATION->fslen,
			( EINK_CURRENT_DISPLAY_CONFIGURATION->felen << 8 ) | EINK_CURRENT_DISPLAY_CONFIGURATION->fblen,
			EINK_CURRENT_DISPLAY_CONFIGURATION->lslen,
			( EINK_CURRENT_DISPLAY_CONFIGURATION->lelen << 8 ) | EINK_CURRENT_DISPLAY_CONFIGURATION->lblen,
			EINK_CURRENT_DISPLAY_CONFIGURATION->pixclkdiv,
	};
	eink_perform_command(EINK_CMD_INIT_DSPE_TMG, dspe_tmg, 5, 0, 0);
	
	const u_int16_t waveform_address[2] = {0x886, 0};
	eink_perform_command(EINK_CMD_RD_WFM_INFO, waveform_address, 2, 0, 0);
	
	eink_perform_command(EINK_CMD_UPD_GDRV_CLR, 0, 0, 0, 0);
	eink_perform_command(EINK_CMD_WAIT_DSPE_TRG, 0, 0, 0, 0);
	
	eink_wait_for_completion();
	
	u_int16_t status = eink_read_register(0x338);
	if(status & 0x08F00) {
		eink_end_use();
		return -EINK_ERROR_CHECKSUM_ERROR;
	}
	
	eink_init_rotmode(EINK_ROTATION_MODE_90);
	
	/* from example code */
	eink_write_register(0x01A, 4);
	eink_write_register(0x320, 0);
	
	/* Set eInk controller SDRAM column address count to 512 */
	eink_write_register(0x100, (eink_read_register(0x100) & ~0x6) | 0x1<<1 );
	/* Set eInk controller SDRAM size to 32MB */
	eink_write_register(0x10A, eink_read_register(0x10A) | 6);
	
	
	eink_was_initialized = 1;
	eink_end_use();
	return 0;
}

/* Note: wait at least 4ms between eink_interface_init() and eink_controller_reset() */
void eink_interface_init(void)
{
	/* The eInk controller is connected to the External Bus Interface on NCS0 (PC23) */
	
	/* Reset for the eInk controller (PIO output) */
	AT91F_PIO_ClearOutput(FPC_NRESET_PIO, FPC_NRESET_PIN);
	AT91F_PIO_CfgOutput(FPC_NRESET_PIO, FPC_NRESET_PIN);
	
	/* According to the spec this pin must be set high */
	AT91F_PIO_SetOutput(FPC_NRMODE_PIO, FPC_NRMODE_PIN);
	AT91F_PIO_CfgOutput(FPC_NRMODE_PIO, FPC_NRMODE_PIN);
	
	/* High when the eInk controller is ready for a command (PIO input) */
	AT91F_PIO_CfgInput(FPC_NHRDY_PIO, FPC_NHRDY_PIN);
	
	/* HD/C selects between command and data (PIO output) */
	AT91F_PIO_SetOutput(FPC_HDC_PIO, FPC_HDC_PIN);
	AT91F_PIO_CfgOutput(FPC_HDC_PIO, FPC_HDC_PIN);
	
	/* D0-15, Write Enable, Read Enable and Chip Select go to the Static Memory Controller peripheral */
	AT91F_PIO_CfgPeriph(FPC_SMC_PIO, 0xFFFFL, FPC_SMC_PINS);
	
	/* 16-bit device on 16-bit interface, no extra float time, wait state enabled, 3 extra wait states (4 total),
	 * no extra setup or hold time. */
	AT91C_BASE_SMC->SMC2_CSR[0] = AT91C_SMC2_BAT | AT91C_SMC2_DBW_16 | (0L<<8) |
		AT91C_SMC2_WSEN | 3 | (0L<<24) | (0L<<28);
	
	pio_irq_init_once();
	vSemaphoreCreateBinary(eink_irq_semaphore);
	eink_memory_access_mutex = xSemaphoreCreateMutex();
	eink_general_access_mutex = xSemaphoreCreateMutex();
	eink_usage_counter_access_mutex = xSemaphoreCreateMutex();
	AT91F_PIO_CfgInput(FPC_HIRQ_PIO, FPC_HIRQ_PIN);
	pio_irq_register(FPC_HIRQ_PIO, FPC_HIRQ_PIN, eink_irq);
	xTaskCreate (eink_main_task, (signed portCHAR *) "EINK MAIN TASK", TASK_EINK_STACK, NULL, TASK_EINK_PRIORITY, NULL);
}
