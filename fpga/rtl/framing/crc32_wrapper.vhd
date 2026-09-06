--------------------------------------------------------------------------------
-- crc32_wrapper.vhd   (VHDL-93 / 2002 uyumlu)
--
-- frame_gen ile serializer arasina girer.
--
--   frame_gen --data--> crc32_wrapper --data--> serializer
--             <--req---               <--req---
--
-- FRAME_LEN byte'i oldugu gibi gecirir, ayni anda CRC biriktirir.
-- Sonra 4 byte CRC basar ve bu sirada frame_gen'e talep GONDERMEZ.
--
--   Cikan cerceve : [35 byte frame][4 byte CRC] = 39 byte
--
-- CRC-32/ISO-HDLC  (zlib.crc32 ile ayni)
--   poly 0xEDB88320 (yansimali)  init 0xFFFFFFFF  xorout 0xFFFFFFFF
--   check("123456789") = 0xCBF43926
--
-- Reset AKTIF DUSUK.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity crc32_wrapper is
    generic (
        FRAME_LEN  : natural := 35;        -- CRC oncesi cerceve uzunlugu
        BIG_ENDIAN : boolean := false      -- true: CRC[31:24] once
    );
    port (
        clk        : in  std_logic;
        rstn       : in  std_logic;

        -- serializer tarafi (asagi)
        byte_req_i : in  std_logic;
        data_o     : out std_logic_vector(7 downto 0);

        -- frame_gen tarafi (yukari)
        data_i     : in  std_logic_vector(7 downto 0);
        byte_req_o : out std_logic
    );
end entity crc32_wrapper;

architecture rtl of crc32_wrapper is

    constant TOTAL_LEN : natural := FRAME_LEN + 4;
    constant CRC_INIT  : std_logic_vector(31 downto 0) := x"FFFFFFFF";

    component crc is
        port (
            crcIn  : in  std_logic_vector(31 downto 0);
            data   : in  std_logic_vector(7 downto 0);
            crcOut : out std_logic_vector(31 downto 0)
        );
    end component;

    signal cnt      : integer range 0 to TOTAL_LEN-1;
    signal crc_reg  : std_logic_vector(31 downto 0);
    signal crc_next : std_logic_vector(31 downto 0);
    signal crc_fin  : std_logic_vector(31 downto 0);
    signal data_mux : std_logic_vector(7 downto 0);
    signal req_gate : std_logic;

begin

    crc_fin <= not crc_reg;                       -- xorout

    ----------------------------------------------------------------
    -- Cikis mux'i
    --   cnt <  FRAME_LEN : frame_gen'den gecir
    --   cnt >= FRAME_LEN : CRC byte'i
    --
    -- VHDL-93'te process(all) yok, duyarlilik listesi acik yazildi.
    ----------------------------------------------------------------
    MUX_PROC : process (cnt, data_i, crc_fin)
        variable k : integer range 0 to 3;
    begin
        if cnt < FRAME_LEN then
            data_mux <= data_i;
        else
            if BIG_ENDIAN then
                k := 3 - (cnt - FRAME_LEN);
            else
                k := cnt - FRAME_LEN;
            end if;

            case k is
                when 0      => data_mux <= crc_fin(7  downto  0);
                when 1      => data_mux <= crc_fin(15 downto  8);
                when 2      => data_mux <= crc_fin(23 downto 16);
                when others => data_mux <= crc_fin(31 downto 24);
            end case;
        end if;
    end process MUX_PROC;

    data_o <= data_mux;

    ----------------------------------------------------------------
    -- CRC XOR agaci - su an cikista duran byte'i katar
    ----------------------------------------------------------------
    u_tree : crc
        port map (
            crcIn  => crc_reg,
            data   => data_mux,
            crcOut => crc_next
        );

    ----------------------------------------------------------------
    -- Talebi yukari ilet
    --   Sadece frame_gen'den byte cekiyorken.
    --   cnt = TOTAL_LEN-1'de de aciyoruz: son CRC byte'i gitti,
    --   frame_gen sonraki cercevenin ilk byte'ini hazirlasin.
    ----------------------------------------------------------------
    GATE_PROC : process (cnt)
    begin
        if (cnt < FRAME_LEN-1) or (cnt = TOTAL_LEN-1) then
            req_gate <= '1';
        else
            req_gate <= '0';
        end if;
    end process GATE_PROC;

    byte_req_o <= byte_req_i and req_gate;

    ----------------------------------------------------------------
    -- Sayac + CRC birikteci
    ----------------------------------------------------------------
    SEQ_PROC : process (clk)
    begin
        if rising_edge(clk) then
            if rstn = '0' then
                cnt     <= 0;
                crc_reg <= CRC_INIT;

            elsif byte_req_i = '1' then
                -- Su an cikista duran byte tuketildi

                if cnt = TOTAL_LEN-1 then
                    cnt     <= 0;
                    crc_reg <= CRC_INIT;
                else
                    cnt <= cnt + 1;
                    if cnt < FRAME_LEN then
                        crc_reg <= crc_next;
                    end if;
                end if;
            end if;
        end if;
    end process SEQ_PROC;

end architecture rtl;