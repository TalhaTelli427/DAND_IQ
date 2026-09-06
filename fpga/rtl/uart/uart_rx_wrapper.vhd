--------------------------------------------------------------------------------
-- uart_rx_wrapper.vhd
--
-- TT_UART_RX sarmalayicisi. FIFO ICERMEZ - block design'da harici baglanir.
--
-- Ne yapiyor:
--   - Reset polaritesini cevirir (disari rstn, iceride rst='1')
--   - Generic'leri 36 MHz / 57600 baud'a ayarlar
--   - Cikislari dogrudan FIFO'nun FIFO_WRITE portuna baglanacak sekilde verir
--
-- 36 MHz / 57600 = 625  -> tam bolunuyor, baud hatasi %0
--
-- Block design baglantisi:
--     wr_data  ->  fifo_generator_0.din
--     wr_en    ->  fifo_generator_0.wr_en
--     full     <-  fifo_generator_0.full
--
-- Reset AKTIF DUSUK.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity uart_rx_wrapper is
    generic (
        clk_hz     : integer := 36_000_000;
        uart_baud  : integer := 57_600;
        data_width : integer := 8
    );
    port (
        clk      : in  std_logic;
        rstn     : in  std_logic;                                   -- AKTIF DUSUK

        rx_pin   : in  std_logic;                                   -- fiziksel UART pini

        full     : in  std_logic;                                   -- FIFO'dan
        wr_data  : out std_logic_vector(data_width-1 downto 0);     -- FIFO din
        wr_en    : out std_logic;                                   -- FIFO wr_en

        overflow : out std_logic                                    -- tani: veri kaybi oldu
    );
end entity uart_rx_wrapper;

architecture rtl of uart_rx_wrapper is

    signal rst_high  : std_logic;
    signal uart_data : std_logic_vector(data_width-1 downto 0);
    signal uart_vld  : std_logic;
    signal ovf_ff    : std_logic := '0';

begin

    rst_high <= not rstn;

    u_uart : entity work.TT_UART_RX
        generic map (
            clk_hz    => clk_hz,
            uart_baud => uart_baud,
            data_size => data_width
        )
        port map (
            clk      => clk,
            rst      => rst_high,
            DATA_R   => uart_data,
            RX       => rx_pin,
            rx_ready => uart_vld
        );

    -- FIFO doluysa yazma, yoksa sessizce veri kaybi olur
    wr_data <= uart_data;
    wr_en   <= uart_vld and (not full);

    -- Tasma oldu mu? Yapiskan bayrak, reset'e kadar kalir.
    ovf_proc : process(clk)
    begin
        if rising_edge(clk) then
            if rstn = '0' then
                ovf_ff <= '0';
            elsif uart_vld = '1' and full = '1' then
                ovf_ff <= '1';
            end if;
        end if;
    end process;

    overflow <= ovf_ff;

end architecture rtl;