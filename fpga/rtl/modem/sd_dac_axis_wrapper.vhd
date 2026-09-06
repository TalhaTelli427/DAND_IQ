library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sd_dac_axis_wrapper is
    generic (
        AXIS_DATA_WIDTH : integer := 32;   -- FIR tam hassasiyet 31 bit -> bayt hizali 32
        SHIFT           : integer := 16;   -- RRC ölçeklemesi için 16'ya çekildi
        IW              : integer := 12    -- sigma-delta giris genisligi
    );
    port (
        clk           : in  std_logic;
        rst           : in  std_logic;     -- AKTIF YUKSEK (sd_mod1 ile ayni)

        -- FIR filtre hýzýný senkronize etmek için eklenen giriþ (400 kHz)
        o_After_FIR   : in  std_logic;

        -- AXI4-Stream Slave (FIR Compiler'dan)
        s_axis_tdata  : in  std_logic_vector(AXIS_DATA_WIDTH-1 downto 0);
        s_axis_tvalid : in  std_logic;
        s_axis_tready : out std_logic;

        -- FPGA fiziksel pin (1 bit PDM -> SX1255 I_IN / Q_IN)
        tx_out        : out std_logic
    );
end entity;

architecture rtl of sd_dac_axis_wrapper is

    signal din_reg : signed(IW-1 downto 0) := (others => '0');
    signal wide    : signed(AXIS_DATA_WIDTH-1 downto 0);

begin

    -- FIR filtrenin veri üretim hýzýný o_After_FIR (400 kHz) ile senkronize et
    s_axis_tready <= o_After_FIR;
    wide <= signed(s_axis_tdata);

    ----------------------------------------------------------------------
    -- ZOH: 400 kHz ornegi bir sonrakine kadar tut
    ----------------------------------------------------------------------
    process(clk)
        variable v : signed(AXIS_DATA_WIDTH-1 downto 0);
    begin
        if rising_edge(clk) then
            if rst = '0' then
                din_reg <= (others => '0');
            elsif s_axis_tvalid = '1' and o_After_FIR = '1' then
                v := shift_right(wide, SHIFT);
                if    v >  2047 then din_reg <= to_signed( 2047, IW);
                elsif v < -2048 then din_reg <= to_signed(-2048, IW);
                else                 din_reg <= resize(v, IW);
                end if;
            end if;
        end if;
    end process;
    ----------------------------------------------------------------------
    -- Sigma-delta: 36 MHz'de surekli, tutulan degeri okuyor
    ----------------------------------------------------------------------
    u_sd : entity work.sd_mod1
        generic map (IW => IW)
        port map (
            clk  => clk,
            rst  => rst,
            en   => '1',
            din  => din_reg,
            dout => tx_out
        );

end architecture rtl;