/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : SX1255 interaktif kontrol konsolu (v7.0)
  *                   Renkli / satir-editorlu UART konsolu.
  *                   RX/TX/PA kontrolu, frekans, kazanc, register dokumu.
  *                   433 MHz Mode A, 36 MHz xtal. USART2 @ 115200 8N1
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ---- SX1255 register haritasi ---- */
#define SX_MODE        0x00
#define SX_FRFH_RX     0x01
#define SX_FRFM_RX     0x02
#define SX_FRFL_RX     0x03
#define SX_FRFH_TX     0x04
#define SX_FRFM_TX     0x05
#define SX_FRFL_TX     0x06
#define SX_VERSION     0x07
#define SX_TXFE1       0x08   /* [6:4] dac_gain  [3:0] mixer_gain */
#define SX_TXFE2       0x09
#define SX_TXFE3       0x0A
#define SX_TXFE4       0x0B
#define SX_RXFE1       0x0C   /* [7:5] lna_gain  [4:1] pga_gain  [0] zin */
#define SX_RXFE2       0x0D
#define SX_RXFE3       0x0E
#define SX_IO_MAP      0x0F
#define SX_CK_SEL      0x10
#define SX_STAT        0x11
#define SX_IISM        0x12
#define SX_DIG_BRIDGE  0x13
#define SX_REG_MAX     0x13
#define SX_WRITE_FLAG  0x80

/* ---- MODE register bitleri ---- */
#define M_REF   0x01
#define M_RX    0x02
#define M_TX    0x04
#define M_DRV   0x08

/* ---- STAT register bitleri ---- */
#define S_PLL_TX 0x01
#define S_PLL_RX 0x02
#define S_XOSC   0x04

#define F_XOSC_HZ         36000000UL
#define F_MIN_HZ          400000000UL
#define F_MAX_HZ          510000000UL
#define RXBUF             96
#define STATUS_PERIOD_MS  1000
#define HEARTBEAT_MS      500

/* ---- ANSI renk kodlari ---- */
#define A_RST   "\033[0m"
#define A_BOLD  "\033[1m"
#define A_DIM   "\033[2m"
#define A_RED   "\033[91m"
#define A_GRN   "\033[92m"
#define A_YEL   "\033[93m"
#define A_CYN   "\033[96m"
#define A_MAG   "\033[95m"
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
volatile uint8_t  sx_alive    = 0;
volatile uint8_t  ver_id      = 0;
volatile uint8_t  stat_reg    = 0;
volatile uint8_t  mode_shadow = M_REF;
volatile uint8_t  mon_on      = 0;
volatile uint8_t  ansi_on     = 1;

/* Son yazilan degerlerin golgesi -> 'stat' ekraninda gostermek icin */
uint32_t f_rx_hz = 433000000UL;
uint32_t f_tx_hz = 433000000UL;
uint8_t  tx_dac  = 3;
uint8_t  tx_mix  = 15;
uint8_t  rx_lna  = 1;
uint8_t  rx_pga  = 15;

uint8_t  rx_dma[RXBUF];
char     acc[RXBUF];
volatile uint16_t acc_len = 0;
char     line[RXBUF];
volatile uint8_t  line_ready = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
uint8_t  sx_read(uint8_t addr);
void     sx_write(uint8_t addr, uint8_t val);
void     sx_manual_reset(void);
uint32_t sx_hz_to_frf(uint32_t hz);
void     sx_set_freq(uint8_t is_tx, uint32_t hz);
void     sx_apply_mode(void);
uint8_t  sx_bringup(void);
void     process_line(char *s);
void     print_banner(void);
void     print_help(void);
void     print_stat(void);
void     print_info(void);
void     print_prompt(void);
void     print_txgain(uint8_t dac, uint8_t mix);
void     uart_str(const char *s);
void     uart_ch(char c);
void     uart_hex(uint8_t b);
void     uart_u32(uint32_t v);
void     uart_i32(int32_t v);
void     uart_mhz(uint32_t hz);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static inline void SX_CS_LOW(void)  { HAL_GPIO_WritePin(CS_SIGNAL_GPIO_Port, CS_SIGNAL_Pin, GPIO_PIN_RESET); }
static inline void SX_CS_HIGH(void) { HAL_GPIO_WritePin(CS_SIGNAL_GPIO_Port, CS_SIGNAL_Pin, GPIO_PIN_SET);  }

/* ========================= UART yardimcilari ========================= */

void uart_str(const char *s) { HAL_UART_Transmit(&huart2, (uint8_t*)s, strlen(s), HAL_MAX_DELAY); }
void uart_ch(char c)         { HAL_UART_Transmit(&huart2, (uint8_t*)&c, 1, HAL_MAX_DELAY); }

/* ANSI kodunu sadece renk aciksa bas */
static void col(const char *code) { if (ansi_on) uart_str(code); }

void uart_hex(uint8_t b)
{
    const char h[] = "0123456789ABCDEF";
    char out[4] = { '0', 'x', h[b >> 4], h[b & 0x0F] };
    HAL_UART_Transmit(&huart2, (uint8_t*)out, 4, HAL_MAX_DELAY);
}
void uart_u32(uint32_t v)
{
    char tmp[12], buf[12]; uint8_t n = 0, i = 0;
    if (v == 0) { uart_str("0"); return; }
    while (v && n < 12) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) buf[i++] = tmp[--n];
    HAL_UART_Transmit(&huart2, (uint8_t*)buf, i, HAL_MAX_DELAY);
}
void uart_i32(int32_t v)
{
    if (v < 0) { uart_str("-"); v = -v; }
    else        uart_str("+");
    uart_u32((uint32_t)v);
}
/* 433000000 -> "433.000 MHz" */
void uart_mhz(uint32_t hz)
{
    uint32_t mhz = hz / 1000000UL;
    uint32_t khz = (hz % 1000000UL) / 1000UL;
    uart_u32(mhz);
    uart_str(".");
    if (khz < 100) uart_str("0");
    if (khz < 10)  uart_str("0");
    uart_u32(khz);
    uart_str(" MHz");
}
/* Sabit genislikte etiket: "  XOSC     : " */
static void field(const char *name)
{
    uart_str("  ");
    uart_str(name);
    uint8_t n = (uint8_t)strlen(name);
    while (n < 9) { uart_str(" "); n++; }
    uart_str(": ");
}
static void tag_ok(const char *txt)   { col(A_GRN); uart_str("[OK] "); uart_str(txt); col(A_RST); }
static void tag_bad(const char *txt)  { col(A_RED); uart_str("[!!] "); uart_str(txt); col(A_RST); }
static void tag_off(const char *txt)  { col(A_DIM); uart_str("[--] "); uart_str(txt); col(A_RST); }

static void msg_ok(const char *s)   { col(A_GRN); uart_str("  [OK] "); uart_str(s); col(A_RST); uart_str("\r\n"); }
static void msg_err(const char *s)  { col(A_RED); uart_str("  [!!] "); uart_str(s); col(A_RST); uart_str("\r\n"); }
static void msg_warn(const char *s) { col(A_YEL); uart_str("  [??] "); uart_str(s); col(A_RST); uart_str("\r\n"); }
static void msg_use(const char *s)  { col(A_DIM); uart_str("       kullanim: "); uart_str(s); col(A_RST); uart_str("\r\n"); }

/* ========================= SX1255 alt seviye ========================= */

uint8_t sx_read(uint8_t addr)
{
    uint8_t tx[2] = { (uint8_t)(addr & 0x7F), 0x00 };
    uint8_t rx[2] = { 0, 0 };
    SX_CS_LOW();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, HAL_MAX_DELAY);
    SX_CS_HIGH();
    return rx[1];
}
void sx_write(uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(addr | SX_WRITE_FLAG), val };
    SX_CS_LOW();
    HAL_SPI_Transmit(&hspi1, tx, 2, HAL_MAX_DELAY);
    SX_CS_HIGH();
}
void sx_manual_reset(void)
{
    HAL_GPIO_WritePin(SX_RESET_GPIO_Port, SX_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(SX_RESET_GPIO_Port, SX_RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(6);
}

uint32_t sx_hz_to_frf(uint32_t hz)
{
    return (uint32_t)(((uint64_t)hz * 1048576ULL) / (uint64_t)F_XOSC_HZ);
}
static uint32_t parse_mhz_to_hz(const char *s)
{
    uint32_t ip = 0, frac = 0, div = 1;
    while (*s >= '0' && *s <= '9') { ip = ip * 10 + (uint32_t)(*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9' && div < 1000000UL) {
            frac = frac * 10 + (uint32_t)(*s - '0'); div *= 10; s++;
        }
    }
    uint64_t hz = (uint64_t)ip * 1000000ULL + (uint64_t)frac * 1000000ULL / div;
    return (uint32_t)hz;
}

void sx_set_freq(uint8_t is_tx, uint32_t hz)
{
    if (hz < F_MIN_HZ || hz > F_MAX_HZ)
        msg_warn("frekans 400-510 MHz bandinin disinda, PLL kilitlenmeyebilir");

    uint32_t frf = sx_hz_to_frf(hz);
    uint8_t h = (uint8_t)((frf >> 16) & 0xFF);
    uint8_t m = (uint8_t)((frf >> 8)  & 0xFF);
    uint8_t l = (uint8_t)( frf        & 0xFF);
    uint8_t base = is_tx ? SX_FRFH_TX : SX_FRFH_RX;

    sx_write(base + 0, h);
    sx_write(base + 1, m);
    sx_write(base + 2, l);

    if (is_tx) f_tx_hz = hz; else f_rx_hz = hz;

    uart_str("  ");
    col(A_CYN); uart_str(is_tx ? "TX" : "RX"); col(A_RST);
    uart_str(" frekans -> ");
    col(A_BOLD); uart_mhz(hz); col(A_RST);
    col(A_DIM);
    uart_str("   (Frf="); uart_hex(h); uart_hex(m); uart_hex(l); uart_str(")");
    col(A_RST);
    uart_str("\r\n");
}

static uint8_t wait_stat(uint8_t mask, uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < ms) {
        stat_reg = sx_read(SX_STAT);
        if (stat_reg & mask) return 1;
        HAL_Delay(2);
    }
    return 0;
}

void sx_apply_mode(void)
{
    sx_write(SX_MODE, mode_shadow);
    HAL_Delay(5);

    uart_str("  MODE = "); uart_hex(mode_shadow); uart_str("   ");
    col(A_DIM);
    uart_str("ref=");  uart_str((mode_shadow & M_REF) ? "1" : "0");
    uart_str("  rx="); uart_str((mode_shadow & M_RX)  ? "1" : "0");
    uart_str("  tx="); uart_str((mode_shadow & M_TX)  ? "1" : "0");
    uart_str("  pa="); uart_str((mode_shadow & M_DRV) ? "1" : "0");
    col(A_RST);
    uart_str("\r\n");

    if (mode_shadow & M_RX) {
        if (wait_stat(S_PLL_RX, 300)) msg_ok("RX PLL kilitlendi");
        else                          msg_err("RX PLL KILITLENMEDI - frekansi ve xtal'i kontrol et");
    }
    if (mode_shadow & M_TX) {
        if (wait_stat(S_PLL_TX, 300)) msg_ok("TX PLL kilitlendi");
        else                          msg_err("TX PLL KILITLENMEDI - frekansi ve xtal'i kontrol et");
    }
}

/* TX kazanci raporu.
 * Datasheet: mixer gain ~= -37.5 dB + 2*mix  (bagil)
 *            dac gain  : 0 -> max-9, 1 -> max-6, 2 -> max-3, 3 -> max (0 dBFS)
 * Mutlak cikis gucu antene/balun'a/kalibrasyona bagli. Bunlar BAGIL gostergedir. */
void print_txgain(uint8_t dac, uint8_t mix)
{
    int32_t mix_db = -37 + 2 * (int32_t)mix;
    const int8_t dac_off[4] = { -9, -6, -3, 0 };
    int32_t dac_db = dac_off[dac & 3];
    int32_t approx = 7 + dac_db + (mix_db - (-37 + 2 * 15));

    uart_str("  TX kazanc -> dac="); uart_u32(dac);
    col(A_DIM); uart_str(" ("); uart_i32(dac_db); uart_str(" dB)"); col(A_RST);
    uart_str("  mix="); uart_u32(mix);
    col(A_DIM); uart_str(" ("); uart_i32(mix_db); uart_str(" dB bagil)"); col(A_RST);
    uart_str("\r\n");

    uart_str("  yaklasik cikis: ");
    col(A_YEL); uart_i32(approx); uart_str(" dBm"); col(A_RST);
    col(A_DIM); uart_str("   (kaba kestirim, guc-metre ile dogrula)"); col(A_RST);
    uart_str("\r\n");
}

uint8_t sx_bringup(void)
{
    ver_id = sx_read(SX_VERSION);
    if (ver_id == 0x00 || ver_id == 0xFF) {
        msg_err("SPI'dan cevap yok (VER=0x00/0xFF) - MOSI/MISO/SCK/CS, besleme, RESET hattini kontrol et");
        return 0;
    }

    sx_write(SX_MODE, M_REF);
    if (!wait_stat(S_XOSC, 500)) {
        msg_err("XOSC kilitlenmedi - 36 MHz kristal / TCXO beslemesini kontrol et");
        return 0;
    }

    sx_write(SX_CK_SEL, 0x02);
        sx_write(SX_IISM,   0x00);
        sx_write(SX_RXFE1,  0x3F);   /* lna=1 (max), pga=15 */
        sx_write(SX_RXFE2,  0xF5);
        rx_lna = 1; rx_pga = 15;

        // 1. DEĞİŞİKLİK: Başlangıç frekansı 433 MHz yapıldı
        sx_set_freq(0, 446100000UL);
        sx_set_freq(1, 446100000UL);

        // 2. DEĞİŞİKLİK: TX Gücü yarıya düşürüldü (-3 dB)
        // dac=2 (-3 dBFS), mix=15 -> 0x2F (Binary: 0010 1111)
        sx_write(SX_TXFE1, 0x2F);
        tx_dac = 1; tx_mix = 5;

        /* REF + RX + TX + PA hepsi acik */
        mode_shadow = M_REF | M_RX | M_TX | M_DRV;
        sx_write(SX_MODE, mode_shadow);
    if (!wait_stat(S_PLL_RX, 300)) {
        msg_err("RX PLL kilitlenmedi");
        return 0;
    }

    if (!wait_stat(S_PLL_TX, 300)) {
        msg_err("TX PLL kilitlenmedi");
        return 0;
    }

    return 1;
}

/* ========================= Ekranlar ========================= */

void print_banner(void)
{
    uart_str("\r\n");
    col(A_CYN);
    uart_str("  .***************************************************=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%####=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%######:+#=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#%%%%%%####+..+##=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#####-. .=###=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#####.    -####=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#########*.   -#####=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%%%%%%%%###########:-#*.  :######=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%%%%#############:  -#*. .#######=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%%%##############-    -#*.:########=\r\n");
    uart_str("  .#%%%%%%%%%%%%%%%%##############=.     -#*.#########=\r\n");
    uart_str("  .#%#%%%%%%%%%%%%%#############+.       -#*##########=\r\n");
    uart_str("  .##%%%%%%%%%%%%#############*.         -############=\r\n");
    uart_str("  .##%%%%%%%%%%%############*.           -############=\r\n");
    uart_str("  .##%%%%%%%%%%############:             -############=\r\n");
    uart_str("  .##%%%%%%%%%###########-.              -############=\r\n");
    uart_str("  .##%%%%%%%%%#########-.                -############=\r\n");
    uart_str("  .##%%%%%%%%########+.                  -############=\r\n");
    uart_str("  .##%%%%%%%#######+.                    -############=\r\n");
    uart_str("  .##%%%%%%%#####*.                      :------------:\r\n");
    uart_str("  .#%#%%%%%#####.   .-================.                  \r\n");
    uart_str("  .#%%%%%%%###. .:.=##################. :++++++++.       \r\n");
    uart_str("  .#%%%%%%%#=.:*:-######- :#*. +######. =**###***.       \r\n");
    uart_str("  .#%%%%##=.=#==#######: =#+..########.   .##*.########- \r\n");
    uart_str("  .#%%%%+.*%#+#######*. +#=.:#########.  .*##+...###=... \r\n");
    uart_str("  .#%%#=#%%#########*. *#=.-##########.  .###-  :###:    \r\n");
    uart_str("  .##*%%%%%%%##########*+###+*########.  .:::.  =###.    \r\n");
    uart_str("  .#%%%%%%%%%%%#######################.         *##=     \r\n");
    uart_str("  .....................................                  \r\n");
    col(A_RST);

    col(A_BOLD);
    uart_str("        K   A   R   G   A     L   A   B   S\r\n");
    col(A_RST);
}
void print_help(void)
{
	print_banner();
    uart_str("\r\n");
    col(A_BOLD); uart_str("  KOMUTLAR"); col(A_RST);
    col(A_DIM);  uart_str("   (buyuk/kucuk harf farketmez)"); col(A_RST);
    uart_str("\r\n\r\n");

    col(A_CYN); uart_str("  -- DURUM --------------------------------------\r\n"); col(A_RST);
    uart_str("   stat              durum ozeti (PLL, mod, frekans, kazanc)\r\n");
    uart_str("   info              tum register dokumu\r\n");
    uart_str("   mon on|off        her saniye otomatik durum yazdir\r\n\r\n");

    col(A_CYN); uart_str("  -- MOD ----------------------------------------\r\n"); col(A_RST);
    uart_str("   rx on|off         RX zinciri\r\n");
    uart_str("   tx on|off         TX zinciri\r\n");
    uart_str("   pa on|off         guc surucusu ");
    col(A_DIM); uart_str("(once 'tx on' gerekir)"); col(A_RST); uart_str("\r\n");
    uart_str("   standby           sadece referans osilator\r\n");
    uart_str("   sleep             tum bloklar kapali\r\n\r\n");

    col(A_CYN); uart_str("  -- FREKANS ------------------------------------\r\n"); col(A_RST);
    uart_str("   freq <MHz>        RX+TX birlikte    ");
    col(A_DIM); uart_str("or: freq 433.5"); col(A_RST); uart_str("\r\n");
    uart_str("   freq rx|tx <MHz>  tek tarafi ayarla\r\n");
    uart_str("   freq              mevcut degerleri goster\r\n\r\n");

    col(A_CYN); uart_str("  -- KAZANC -------------------------------------\r\n"); col(A_RST);
    uart_str("   txpower <0-15>    tek knob TX gucu  ");
    col(A_DIM); uart_str("(0=min 15=max)"); col(A_RST); uart_str("\r\n");
    uart_str("   txgain <dac 0-3> <mix 0-15>\r\n");
    uart_str("   rxgain <lna 1-6> <pga 0-15>   ");
    col(A_DIM); uart_str("(lna 1 = en yuksek kazanc)"); col(A_RST); uart_str("\r\n\r\n");

    col(A_CYN); uart_str("  -- HAM ERISIM ---------------------------------\r\n"); col(A_RST);
    uart_str("   rd <adr>          register oku     ");
    col(A_DIM); uart_str("or: rd 0x11"); col(A_RST); uart_str("\r\n");
    uart_str("   reg <adr> <deg>   register yaz     ");
    col(A_DIM); uart_str("or: reg 0x08 0x3F"); col(A_RST); uart_str("\r\n");
    uart_str("   reset             donanim reset + yeniden bring-up\r\n\r\n");

    col(A_CYN); uart_str("  -- DIGER --------------------------------------\r\n"); col(A_RST);
    uart_str("   ansi on|off       renkli cikti\r\n");
    uart_str("   help  /  ?        bu ekran\r\n\r\n");

    col(A_DIM);
    uart_str("  Ipucu: once 'stat' ile XOSC ve PLL kilidini dogrula.\r\n");
    uart_str("  Cikis gucu ~+7 dBm ile sinirlidir; fazlasi icin harici PA.\r\n");
    col(A_RST);
}

void print_stat(void)
{
    stat_reg      = sx_read(SX_STAT);
    uint8_t m     = sx_read(SX_MODE);
    uint8_t txfe1 = sx_read(SX_TXFE1);
    uint8_t rxfe1 = sx_read(SX_RXFE1);

    uart_str("\r\n");
    col(A_BOLD); uart_str("  ---- DURUM -----------------------------------\r\n"); col(A_RST);

    field("XOSC");
    if (stat_reg & S_XOSC) tag_ok("kilitli"); else tag_bad("kilitlenmedi");
    uart_str("\r\n");

    field("PLL RX");
    if (!(m & M_RX))            tag_off("kapali");
    else if (stat_reg & S_PLL_RX) tag_ok("kilitli");
    else                          tag_bad("kilitlenmedi");
    uart_str("\r\n");

    field("PLL TX");
    if (!(m & M_TX))            tag_off("kapali");
    else if (stat_reg & S_PLL_TX) tag_ok("kilitli");
    else                          tag_bad("kilitlenmedi");
    uart_str("\r\n");

    field("PA");
    if (m & M_DRV) { col(A_YEL); uart_str("[ON] surucu aktif"); col(A_RST); }
    else           tag_off("kapali");
    uart_str("\r\n");

    field("MODE");
    uart_hex(m);
    col(A_DIM);
    uart_str("   ref="); uart_str((m & M_REF) ? "1" : "0");
    uart_str(" rx=");    uart_str((m & M_RX)  ? "1" : "0");
    uart_str(" tx=");    uart_str((m & M_TX)  ? "1" : "0");
    uart_str(" pa=");    uart_str((m & M_DRV) ? "1" : "0");
    col(A_RST);
    uart_str("\r\n");

    field("RX frek");  uart_mhz(f_rx_hz); uart_str("\r\n");
    field("TX frek");  uart_mhz(f_tx_hz); uart_str("\r\n");

    field("TX gain");
    uart_str("dac="); uart_u32((uint32_t)((txfe1 >> 4) & 0x07));
    uart_str("  mix="); uart_u32((uint32_t)(txfe1 & 0x0F));
    col(A_DIM); uart_str("   TXFE1="); uart_hex(txfe1); col(A_RST);
    uart_str("\r\n");

    field("RX gain");
    uart_str("lna="); uart_u32((uint32_t)((rxfe1 >> 5) & 0x07));
    uart_str("  pga="); uart_u32((uint32_t)((rxfe1 >> 1) & 0x0F));
    col(A_DIM); uart_str("   RXFE1="); uart_hex(rxfe1); col(A_RST);
    uart_str("\r\n");

    field("Chip");
    uart_str("VER="); uart_hex(ver_id);
    col(A_DIM); uart_str("   STAT="); uart_hex(stat_reg); col(A_RST);
    uart_str("\r\n");

    col(A_BOLD); uart_str("  ----------------------------------------------\r\n"); col(A_RST);
}

static const char *reg_name(uint8_t a)
{
    switch (a) {
        case 0x00: return "MODE";
        case 0x01: return "FRFH_RX";
        case 0x02: return "FRFM_RX";
        case 0x03: return "FRFL_RX";
        case 0x04: return "FRFH_TX";
        case 0x05: return "FRFM_TX";
        case 0x06: return "FRFL_TX";
        case 0x07: return "VERSION";
        case 0x08: return "TXFE1";
        case 0x09: return "TXFE2";
        case 0x0A: return "TXFE3";
        case 0x0B: return "TXFE4";
        case 0x0C: return "RXFE1";
        case 0x0D: return "RXFE2";
        case 0x0E: return "RXFE3";
        case 0x0F: return "IO_MAP";
        case 0x10: return "CK_SEL";
        case 0x11: return "STAT";
        case 0x12: return "IISM";
        case 0x13: return "DIG_BRIDGE";
        default:   return "?";
    }
}

void print_info(void)
{
    uart_str("\r\n");
    col(A_BOLD); uart_str("  ---- REGISTER DOKUMU -------------------------\r\n"); col(A_RST);
    for (uint8_t a = 0; a <= SX_REG_MAX; a++) {
        uint8_t v = sx_read(a);
        uart_str("   ");
        uart_hex(a);
        uart_str("  ");
        const char *n = reg_name(a);
        uart_str(n);
        uint8_t k = (uint8_t)strlen(n);
        while (k < 12) { uart_str(" "); k++; }
        uart_hex(v);
        /* ikilik gosterim */
        col(A_DIM);
        uart_str("   b");
        for (int8_t b = 7; b >= 0; b--) uart_str((v & (1 << b)) ? "1" : "0");
        col(A_RST);
        uart_str("\r\n");
    }
    col(A_BOLD); uart_str("  ----------------------------------------------\r\n"); col(A_RST);
}

void print_prompt(void)
{
    col(A_CYN);
    uart_str("[");
    if (mode_shadow == 0x00) {
        uart_str("SLEEP");
    } else if (!(mode_shadow & (M_RX | M_TX))) {
        uart_str("STBY");
    } else {
        if (mode_shadow & M_RX) uart_str("RX");
        if (mode_shadow & M_TX) uart_str((mode_shadow & M_RX) ? "+TX" : "TX");
        if (mode_shadow & M_DRV) uart_str("+PA");
    }
    uart_str(" ");
    uart_mhz((mode_shadow & M_TX) ? f_tx_hz : f_rx_hz);
    uart_str("] > ");
    col(A_RST);
}

/* ========================= Komut isleyici ========================= */

static void str_lower(char *s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32; }
static int  str_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static int onoff(const char *s, uint8_t *out)
{
    if (str_eq(s, "on")  || str_eq(s, "1")) { *out = 1; return 1; }
    if (str_eq(s, "off") || str_eq(s, "0")) { *out = 0; return 1; }
    return 0;
}

void process_line(char *s)
{
    char *argv[6]; int argc = 0;
    char *tok = strtok(s, " \t");
    while (tok && argc < 6) { argv[argc++] = tok; tok = strtok(NULL, " \t"); }
    if (argc == 0) { print_prompt(); return; }

    str_lower(argv[0]);
    uint8_t flag = 0;

    /* ---------- yardim / durum ---------- */
    if (str_eq(argv[0], "help") || str_eq(argv[0], "?") || str_eq(argv[0], "h")) {
        print_help();
    }
    else if (str_eq(argv[0], "stat") || str_eq(argv[0], "s")) {
        print_stat();
    }
    else if (str_eq(argv[0], "info") || str_eq(argv[0], "i")) {
        print_info();
    }
    else if (str_eq(argv[0], "ansi")) {
        if (argc >= 2) { str_lower(argv[1]); if (onoff(argv[1], &flag)) ansi_on = flag; }
        msg_ok(ansi_on ? "renkli cikti ACIK" : "renkli cikti KAPALI");
    }
    else if (str_eq(argv[0], "mon")) {
        if (argc >= 2) { str_lower(argv[1]); if (onoff(argv[1], &flag)) mon_on = flag; else { msg_err("gecersiz deger"); msg_use("mon on|off"); } }
        else { msg_err("eksik argüman"); msg_use("mon on|off"); }
        msg_ok(mon_on ? "otomatik durum ACIK (1 sn)" : "otomatik durum KAPALI");
    }

    /* ---------- mod kontrolu ---------- */
    else if (str_eq(argv[0], "rx")) {
        if (argc < 2) { msg_err("eksik argüman"); msg_use("rx on|off"); }
        else { str_lower(argv[1]);
            if (!onoff(argv[1], &flag)) { msg_err("gecersiz deger"); msg_use("rx on|off"); }
            else { if (flag) mode_shadow |= M_RX; else mode_shadow &= (uint8_t)~M_RX;
                   mode_shadow |= M_REF; sx_apply_mode(); } }
    }
    else if (str_eq(argv[0], "tx")) {
        if (argc < 2) { msg_err("eksik argüman"); msg_use("tx on|off"); }
        else { str_lower(argv[1]);
            if (!onoff(argv[1], &flag)) { msg_err("gecersiz deger"); msg_use("tx on|off"); }
            else { if (flag) mode_shadow |= M_TX; else mode_shadow &= (uint8_t)~(M_TX | M_DRV);
                   mode_shadow |= M_REF; sx_apply_mode(); } }
    }
    else if (str_eq(argv[0], "pa")) {
        if (argc < 2) { msg_err("eksik argüman"); msg_use("pa on|off"); }
        else { str_lower(argv[1]);
            if (!onoff(argv[1], &flag)) { msg_err("gecersiz deger"); msg_use("pa on|off"); }
            else if (flag) {
                if (!(mode_shadow & M_TX)) { msg_warn("TX zinciri kapali - once 'tx on' yaz"); }
                mode_shadow |= (M_TX | M_DRV | M_REF); sx_apply_mode();
            } else { mode_shadow &= (uint8_t)~M_DRV; sx_apply_mode(); } }
    }
    else if (str_eq(argv[0], "standby")) { mode_shadow = M_REF; sx_apply_mode(); msg_ok("STANDBY - sadece referans osilator"); }
    else if (str_eq(argv[0], "sleep"))   { mode_shadow = 0x00; sx_write(SX_MODE, 0x00); msg_ok("SLEEP - tum bloklar kapali"); }

    /* ---------- frekans ---------- */
    else if (str_eq(argv[0], "freq") || str_eq(argv[0], "f")) {
        if (argc == 1) {
            uart_str("  RX: "); uart_mhz(f_rx_hz); uart_str("\r\n");
            uart_str("  TX: "); uart_mhz(f_tx_hz); uart_str("\r\n");
        }
        else if (argc == 2) {                      /* freq 433.5  -> ikisi birden */
            uint32_t hz = parse_mhz_to_hz(argv[1]);
            if (hz == 0) { msg_err("frekans cozulemedi"); msg_use("freq 433.5   |   freq rx 433.5"); }
            else { sx_set_freq(0, hz); sx_set_freq(1, hz); }
        }
        else {
            str_lower(argv[1]);
            uint32_t hz = parse_mhz_to_hz(argv[2]);
            if (hz == 0)                       { msg_err("frekans cozulemedi"); msg_use("freq rx|tx|both <MHz>"); }
            else if (str_eq(argv[1], "rx"))    sx_set_freq(0, hz);
            else if (str_eq(argv[1], "tx"))    sx_set_freq(1, hz);
            else if (str_eq(argv[1], "both"))  { sx_set_freq(0, hz); sx_set_freq(1, hz); }
            else { msg_err("gecersiz hedef"); msg_use("freq rx|tx|both <MHz>"); }
        }
    }

    /* ---------- kazanc ---------- */
    else if (str_eq(argv[0], "txpower") || str_eq(argv[0], "p")) {
        if (argc < 2) {
            uart_str("  mevcut: dac="); uart_u32(tx_dac);
            uart_str("  mix=");        uart_u32(tx_mix); uart_str("\r\n");
            msg_use("txpower <0-15>");
        } else {
            uint32_t p = strtoul(argv[1], NULL, 0);
            if (p > 15) { msg_warn("15 uzeri deger 15'e kirpildi"); p = 15; }
            uint8_t dac = (p >= 12) ? 3 : (p >= 8) ? 2 : (p >= 4) ? 1 : 0;
            uint8_t v = (uint8_t)((dac << 4) | (p & 0x0F));
            sx_write(SX_TXFE1, v);
            tx_dac = dac; tx_mix = (uint8_t)p;
            col(A_DIM); uart_str("  TXFE1="); uart_hex(v); col(A_RST); uart_str("\r\n");
            print_txgain(dac, (uint8_t)p);
        }
    }
    else if (str_eq(argv[0], "txgain")) {
        if (argc < 3) {
            uart_str("  mevcut: dac="); uart_u32(tx_dac);
            uart_str("  mix=");        uart_u32(tx_mix); uart_str("\r\n");
            msg_use("txgain <dac 0-3> <mix 0-15>");
        } else {
            uint32_t d = strtoul(argv[1], NULL, 0);
            uint32_t x = strtoul(argv[2], NULL, 0);
            if (d > 3)  { msg_warn("dac 0-3 araliginda olmali, kirpildi"); d = 3;  }
            if (x > 15) { msg_warn("mix 0-15 araliginda olmali, kirpildi"); x = 15; }
            uint8_t v = (uint8_t)((d << 4) | x);
            sx_write(SX_TXFE1, v);
            tx_dac = (uint8_t)d; tx_mix = (uint8_t)x;
            col(A_DIM); uart_str("  TXFE1="); uart_hex(v); col(A_RST); uart_str("\r\n");
            print_txgain((uint8_t)d, (uint8_t)x);
        }
    }
    else if (str_eq(argv[0], "rxgain")) {
        if (argc < 3) {
            uart_str("  mevcut: lna="); uart_u32(rx_lna);
            uart_str("  pga=");        uart_u32(rx_pga); uart_str("\r\n");
            msg_use("rxgain <lna 1-6> <pga 0-15>");
        } else {
            uint32_t l = strtoul(argv[1], NULL, 0);
            uint32_t g = strtoul(argv[2], NULL, 0);
            if (l < 1 || l > 6) { msg_warn("lna 1-6 araliginda olmali, 1'e ayarlandi"); l = 1; }
            if (g > 15)         { msg_warn("pga 0-15 araliginda olmali, kirpildi");     g = 15; }
            uint8_t cur = sx_read(SX_RXFE1);
            uint8_t v = (uint8_t)((l << 5) | (g << 1) | (cur & 0x01));
            sx_write(SX_RXFE1, v);
            rx_lna = (uint8_t)l; rx_pga = (uint8_t)g;
            uart_str("  RX kazanc -> lna="); uart_u32(l);
            uart_str("  pga="); uart_u32(g);
            col(A_DIM); uart_str("   RXFE1="); uart_hex(v); col(A_RST);
            uart_str("\r\n");
        }
    }

    /* ---------- ham register ---------- */
    else if (str_eq(argv[0], "reg") || str_eq(argv[0], "w")) {
        if (argc < 3) { msg_err("eksik argüman"); msg_use("reg <adr> <deg>    or: reg 0x08 0x3F"); }
        else {
            uint8_t a = (uint8_t)strtoul(argv[1], NULL, 0);
            uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
            sx_write(a, v);
            uint8_t back = sx_read(a);
            uart_str("  ["); uart_hex(a); uart_str("] ");
            uart_str(reg_name(a));
            uart_str("  yazilan="); uart_hex(v);
            uart_str("  okunan="); uart_hex(back);
            if (back == v) { col(A_GRN); uart_str("   OK"); col(A_RST); }
            else           { col(A_YEL); uart_str("   FARKLI (read-only bit olabilir)"); col(A_RST); }
            uart_str("\r\n");
        }
    }
    else if (str_eq(argv[0], "rd") || str_eq(argv[0], "r")) {
        if (argc < 2) { msg_err("eksik argüman"); msg_use("rd <adr>    or: rd 0x11"); }
        else {
            uint8_t a = (uint8_t)strtoul(argv[1], NULL, 0);
            uint8_t v = sx_read(a);
            uart_str("  ["); uart_hex(a); uart_str("] ");
            uart_str(reg_name(a));
            uart_str(" = "); uart_hex(v);
            col(A_DIM);
            uart_str("   b");
            for (int8_t b = 7; b >= 0; b--) uart_str((v & (1 << b)) ? "1" : "0");
            col(A_RST);
            uart_str("\r\n");
        }
    }

    /* ---------- reset ---------- */
    else if (str_eq(argv[0], "reset")) {
        uart_str("  donanim reset...\r\n");
        sx_manual_reset();
        sx_alive = sx_bringup();
        if (sx_alive) msg_ok("bring-up tamam - RX ve TX acik, 433.000 MHz");
        else          msg_err("bring-up BASARISIZ - 'info' ile register'lara bak");
    }

    else {
        col(A_RED); uart_str("  bilinmeyen komut: '"); uart_str(argv[0]); uart_str("'"); col(A_RST);
        uart_str("\r\n");
        col(A_DIM); uart_str("       komut listesi icin 'help' yaz\r\n"); col(A_RST);
    }

    print_prompt();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  SX_CS_HIGH();
  HAL_Delay(10);

  //print_banner();
  uart_str("  bring-up basliyor...\r\n\r\n");

  sx_manual_reset();
  sx_alive = sx_bringup();

  uart_str("\r\n");
  if (sx_alive) {
      col(A_GRN); col(A_BOLD);
      uart_str("  >> HAZIR - RX ve TX acik, 433.000 MHz  (VER="); uart_hex(ver_id); uart_str(")\r\n");
      col(A_RST);
  } else {
      col(A_RED); col(A_BOLD);
      uart_str("  >> BRING-UP BASARISIZ\r\n");
      col(A_RST);
      col(A_DIM);
      uart_str("     'info' ile register dokumune bak, 'reset' ile tekrar dene.\r\n");
      col(A_RST);
  }

  print_help();
  uart_str("\r\n");
  print_prompt();

  HAL_UARTEx_ReceiveToIdle_IT(&huart2, rx_dma, RXBUF);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_status = 0;
  uint32_t last_blink  = 0;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (line_ready) { line_ready = 0; process_line(line); }

    if (mon_on && (HAL_GetTick() - last_status) >= STATUS_PERIOD_MS) {
        last_status = HAL_GetTick();
        print_stat();
        print_prompt();
    }

    /* Heartbeat: saglikliysa hizli, degilse yavas yanip soner */
    uint32_t now = HAL_GetTick();
    uint32_t period = HEARTBEAT_MS;
    if (sx_alive) {
        stat_reg = sx_read(SX_STAT);
        uint8_t healthy = (stat_reg & S_XOSC) &&
                          ((mode_shadow & M_RX) ? (stat_reg & S_PLL_RX) : 1) &&
                          ((mode_shadow & M_TX) ? (stat_reg & S_PLL_TX) : 1);
        period = healthy ? 100 : 500;
    }
    if ((now - last_blink) >= period) {
        last_blink = now;
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, CS_SIGNAL_Pin|SX_RESET_Pin|LED1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : CS_SIGNAL_Pin SX_RESET_Pin LED1_Pin */
  GPIO_InitStruct.Pin = CS_SIGNAL_Pin|SX_RESET_Pin|LED1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* Satir editoru: echo, backspace, Ctrl+U (satiri sil).
 * Not: echo icin blocking HAL_UART_Transmit kullaniliyor; 115200'de
 * karakter basina ~87 us, konsol uygulamasi icin kabul edilebilir. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2) {
        for (uint16_t i = 0; i < Size; i++) {
            char c = (char)rx_dma[i];

            if (c == '\r' || c == '\n') {
                uart_str("\r\n");
                if (acc_len > 0 && !line_ready) {
                    memcpy(line, acc, acc_len);
                    line[acc_len] = 0;
                    line_ready = 1;
                } else if (acc_len == 0) {
                    print_prompt();
                }
                acc_len = 0;
            }
            else if (c == '\b' || c == 0x7F) {           /* backspace / delete */
                if (acc_len > 0) { acc_len--; uart_str("\b \b"); }
            }
            else if (c == 0x15) {                        /* Ctrl+U : satiri sil */
                while (acc_len > 0) { acc_len--; uart_str("\b \b"); }
            }
            else if (c == 0x03) {                        /* Ctrl+C : iptal */
                acc_len = 0;
                uart_str("^C\r\n");
                print_prompt();
            }
            else if (c >= 32 && c < 127 && acc_len < RXBUF - 1) {
                acc[acc_len++] = c;
                uart_ch(c);
            }
        }
        HAL_UARTEx_ReceiveToIdle_IT(&huart2, rx_dma, RXBUF);
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
