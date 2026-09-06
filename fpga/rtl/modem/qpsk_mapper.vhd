library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all; 

entity QPSK_mapper is
    port (
        clk     : in std_logic;
        rst     : in std_logic;
        data_i  : in std_logic_vector(1 downto 0);
        en_100k : in std_logic;
        I_o     : out signed(11 downto 0); 
        Q_o     : out signed(11 downto 0)  
    );
end QPSK_mapper;

architecture rtl of QPSK_mapper is

    constant C_AMP : signed(11 downto 0) := to_signed(1023, 12);
    signal reg_I   : signed(11 downto 0);
    signal reg_Q   : signed(11 downto 0);
    
begin

    QPSK_PROC : process(clk)
    begin
        if rising_edge(clk) then
            if rst = '0' then
                reg_I <= C_AMP;
                reg_Q <= C_AMP;
            else
                if en_100k = '1' then
                    case data_i is
                        when "00" =>
                            reg_I <= reg_I;
                            reg_Q <= reg_Q;
                        when "01" =>
                            reg_I <= -reg_Q;
                            reg_Q <= reg_I;
                        when "11" =>
                            reg_I <= -reg_I;
                            reg_Q <= -reg_Q;
                        when "10" =>
                            reg_I <= reg_Q;
                            reg_Q <= -reg_I;
                        when others =>
                            reg_I <= C_AMP;
                            reg_Q <= C_AMP;
                    end case;
                end if;  
            end if;
        end if;
    end process;

    I_o <= reg_I;
    Q_o <= reg_Q;

end architecture rtl;