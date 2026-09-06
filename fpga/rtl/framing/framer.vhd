library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity framer is
    generic(
        data_width : natural := 8
    );
    port (
        clk          : in  std_logic;
        rstn         : in  std_logic;
        lsfr_in      : in  std_logic_vector(data_width-1 downto 0);
        uart_f_in    : in  std_logic_vector(data_width-1 downto 0);
        fifo_empty   : in  std_logic;
        tick_25k     : in  std_logic;
        fifo_d_count : in  std_logic_vector(data_width-1 downto 0);
        fifo_read_en : out std_logic;
        c_data_out   : out std_logic_vector(data_width-1 downto 0)
    );
end framer;

architecture rtl of framer is

constant key_frame : std_logic_vector(data_width-1 downto 0) := x"65";

type state_t is (ST_KEY1, ST_KEY2, ST_INDEX, ST_DATA);
signal state             : state_t := ST_KEY1;
signal real_signal_count : integer range 0 to 32 := 0;
signal data_count        : integer range 0 to 35 := 0;
signal fifo_d_count_ff   : std_logic_vector(data_width-1 downto 0);
signal data_ff           : std_logic_vector(data_width-1 downto 0);

begin

FSM_PROC : process(clk)
begin
    if rising_edge(clk) then
        if rstn = '0' then
            state             <= ST_KEY1;
            data_count        <= 0;
            real_signal_count <= 0;
            fifo_read_en      <= '0';
            c_data_out        <= (others => '0');
            data_ff           <= (others => '0');
            fifo_d_count_ff   <= (others => '0');

        else
            fifo_read_en <= '0';                 

            if (tick_25k = '1') then
                case state is

                    when ST_KEY1 =>
                        c_data_out      <= key_frame;
                        data_count      <= data_count + 1;
                        fifo_d_count_ff <= fifo_d_count;
                        state           <= ST_KEY2;

                    when ST_KEY2 =>
                        c_data_out <= key_frame;
                        data_count <= data_count + 1;
                        if (unsigned(fifo_d_count_ff) >= 32) then
                            real_signal_count <= 32;
                        else
                            real_signal_count <= to_integer(unsigned(fifo_d_count_ff));
                        end if;
                        state <= ST_INDEX;

                    when ST_INDEX =>
                        c_data_out <= std_logic_vector(
                                        to_unsigned(real_signal_count, data_width));
                        data_count <= data_count + 1;

                        -- ilk payload byte'ini hazirla
                        if (real_signal_count /= 0 and fifo_empty = '0') then
                            fifo_read_en      <= '1';
                            data_ff           <= uart_f_in;
                            real_signal_count <= real_signal_count - 1;
                        else
                            data_ff <= lsfr_in;
                        end if;

                        state <= ST_DATA;

                    when ST_DATA =>
                        c_data_out <= data_ff;             

                        if data_count = 34 then
                            data_count <= 0;
                            state      <= ST_KEY1;
                        else
                            data_count <= data_count + 1;

                            if (real_signal_count /= 0 and fifo_empty = '0') then
                                fifo_read_en      <= '1';
                                data_ff           <= uart_f_in;
                                real_signal_count <= real_signal_count - 1;
                            else
                                data_ff <= lsfr_in;
                            end if;
                        end if;


                end case;
            end if;
        end if;
    end if;
end process;

end architecture;