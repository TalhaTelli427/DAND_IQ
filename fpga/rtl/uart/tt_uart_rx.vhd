library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity TT_UART_RX is
    generic (
        clk_hz    : integer := 100_000_000; 
        uart_baud : integer := 115200;  
        data_size : integer := 8
    );
    Port ( 
        clk       : in STD_LOGIC;
        rst       : in STD_LOGIC;
        DATA_R    : out STD_LOGIC_VECTOR (data_size-1 downto 0);
        RX        : in STD_LOGIC;
        rx_ready  : out STD_LOGIC 
    );
end TT_UART_RX;

architecture Behavioral of TT_UART_RX is

    constant clk_per_bit      : integer := (clk_hz + uart_baud/2) / uart_baud;
    constant clk_per_bit_half : integer := clk_per_bit/2;

    type state_type is (IDLE, START_RX, DATA_RX, STOP_RX);

    signal current_state : state_type  := IDLE;
    signal clk_cnter     : integer range 0 to clk_per_bit-1 := 0; 
    signal bit_cnt       : integer range 0 to data_size-1 := 0;

    signal RX_data       : std_logic_vector(data_size-1 downto 0) := (others => '0');
    signal RX_ff1, RX_ff2 : std_logic := '1';
    attribute ASYNC_REG : string;
    attribute ASYNC_REG of RX_ff1 : signal is "TRUE";
    attribute ASYNC_REG of RX_ff2 : signal is "TRUE";
    attribute DONT_TOUCH : string;
    attribute DONT_TOUCH of RX_ff1 : signal is "TRUE";
    attribute DONT_TOUCH of RX_ff2 : signal is "TRUE";
    signal rx_valid       : std_logic := '0';

begin

    SYNC_PROC : process(clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                RX_ff1 <= '1';                
                RX_ff2 <= '1';   
            else
                RX_ff1 <= RX;
                RX_ff2 <= RX_ff1;
            end if;
        end if;
    end process;

    FSM_PROC : process(clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                current_state <= IDLE;
                bit_cnt       <= 0;
                clk_cnter     <= 0;
                rx_valid      <= '0';
            else
                case current_state is

                    when IDLE =>
                        if (RX_ff2 = '0') then
                            current_state <= START_RX;
                            bit_cnt       <= 0;
                            clk_cnter     <= 0;
                            rx_valid      <= '0'; 
                        else 
                            current_state <= IDLE;
                            rx_valid      <= '0'; 
                        end if;
                        
                    when START_RX =>
                        if (clk_cnter = clk_per_bit_half-1) then
                            if (RX_ff2 = '0') then
                                clk_cnter     <= 0;
                                current_state <= DATA_RX;
                            else
                                current_state <= IDLE;
                            end if;    
                        else
                            clk_cnter     <= clk_cnter + 1;
                            current_state <= START_RX;
                        end if;
                        
                    when DATA_RX =>
                        if (clk_cnter = clk_per_bit-1) then
                            clk_cnter <= 0;
                            RX_data   <= RX_ff2 & RX_data(data_size-1 downto 1);
                            if (bit_cnt = data_size-1) then 
                                current_state <= STOP_RX;
                            else 
                                bit_cnt       <= bit_cnt + 1;
                                current_state <= DATA_RX;
                            end if;
                        else
                            clk_cnter     <= clk_cnter + 1;
                            current_state <= DATA_RX;
                        end if;
                        
                    when STOP_RX =>
                        if (clk_cnter = clk_per_bit-1) then
                            clk_cnter     <= 0;
                            current_state <= IDLE;
                            rx_valid      <= '1';
                            DATA_R        <= RX_data;
                        else
                            clk_cnter     <= clk_cnter + 1;
                            current_state <= STOP_RX;
                        end if;

                end case;
            end if;
        end if;
    end process;

    rx_ready <= rx_valid; 

end architecture;