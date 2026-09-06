--------------------------------------------------------------------------------
-- serializer.vhd
--
-- 8 bit -> 2 bit (dibit) donusturucu. MSB once.
--
--   byte     : [7:6] [5:4] [3:2] [1:0]
--   dibit    :   0     1     2     3
--
-- Sinyaller:
--   en_i       : sembol darbesi (100 kHz) - her darbede bir dibit cikar
--   byte_req_o : byte istegi (25 kHz)     - yukari akisa "yeni byte ver"
--   data_o     : dibit
--   valid_o    : data_o gecerli (en_i'nin 1 cevrim gecikmisi)
--
-- Reset AKTIF DUSUK.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;

entity serializer is
    generic (
        data_width_i : natural := 8;
        data_width_o : natural := 2
    );
    port (
        clk        : in  std_logic;
        rstn       : in  std_logic;

        en_i       : in  std_logic;                                    -- sembol tick
        data_i     : in  std_logic_vector(data_width_i-1 downto 0);
        byte_req_o : out std_logic;                                    -- yukari: byte iste

        valid_o    : out std_logic;                                    -- asagi: dibit gecerli
        data_o     : out std_logic_vector(data_width_o-1 downto 0)
    );
end entity serializer;

architecture rtl of serializer is

    constant C_STEPS : natural := data_width_i / data_width_o;   -- 4

    signal step    : integer range 0 to C_STEPS-1 := 0;
    signal byte_ff : std_logic_vector(data_width_i-1 downto 0) := (others=>'0');

begin

    process (clk)
    begin
        if rising_edge(clk) then
            if rstn = '0' then
                step       <= 0;
                byte_ff    <= (others => '0');
                data_o     <= (others => '0');
                valid_o    <= '0';
                byte_req_o <= '0';

            else
                -- varsayilan: darbeler tek cevrim
                valid_o    <= '0';
                byte_req_o <= '0';

                if en_i = '1' then
                    valid_o <= '1';

                    if step = 0 then
                        -- Yeni byte'i DOGRUDAN girisden al ve ayni anda kaydet.
                        -- Boylece ilk dibit hemen dogru cikar.
                        data_o  <= data_i(data_width_i-1 downto data_width_i-2);
                        byte_ff <= data_i;
                    else
                        -- Kayitli byte'tan sirayla
                        data_o <= byte_ff(data_width_i-1 - step*data_width_o
                                  downto data_width_i - (step+1)*data_width_o);
                    end if;

                    if step = C_STEPS-1 then
                        step       <= 0;
                        byte_req_o <= '1';   -- son dibit gitti, yeni byte iste
                    else
                        step <= step + 1;
                    end if;
                end if;
            end if;
        end if;
    end process;

end architecture rtl;