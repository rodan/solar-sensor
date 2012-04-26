
set terminal png size 700,900
set output 'ss_multi.png' 

set title "sensor data for the last 48 hours"

set multiplot

set tmarg 0
set bmarg 0
set lmarg 7
set rmarg 3
set size 1,0.36
set origin 0.0,0.60

set timefmt "%Y-%m-%dT%H:%M:%S"
set datafile separator "|"


set xdata time
#set xlabel 'time'
#set xrange ['2011-11-25T13:01:08':'2011-11-26T13:01:08']
set xtics 7200
set format x "%H"
#set format x "%H\n%a"

#set ylabel 'temperature (dC)'
#set ytics nomirror
set ytics 2
#set yrange [-20:40]

set grid
set palette model RGB
set palette defined ( 0 '#00FFFF', 1 '#DC143C' )
set cbrange [-10:10]

plot "< sqlite3 /var/lib/ss_daemon/data_tmp.db 'select date, t_ext, t_int, td_ext, t_tk from sensors order by date desc limit 0, 96;'" \
    using 1:2:2 index 0 title "ext temperature (dC)" with lines palette lw 2, \
    "" using 1:3 title "int temperature (dC)" with lines lt rgb '#b22222' lw 2, \
    "" using 1:4 title "ext dew temperature (dC)" with lines lt rgb '#008080' lw 1
#, \
#    "" using 1:5 title "thermocouple (dC)" with lines lt rgb '#f81d8e' lw 1

unset title


# humidity

set tmarg 0
set bmarg 0
set lmarg 7
set rmarg 15
set size 1,0.1
set origin 0.0,0.468

#set ylabel 'humidity (%RH)'
set ytics 20
set yrange [0:100]

plot "< sqlite3 /var/lib/ss_daemon/data_tmp.db 'select date, h_ext from sensors order by date desc limit 0, 96;'" \
    using 1:2 title "ext humidity (%RH)" with lines lt rgb '#0457ef' lw 2


# pressure

set tmarg 0
set bmarg 0
set lmarg 7
set rmarg 15
set size 1,0.1
set origin 0.0,0.336

#set ylabel 'pressure (Pa)'
set ytics 1000
set yrange [96000:102000]

plot "< sqlite3 /var/lib/ss_daemon/data_tmp.db 'select date, p_ext from sensors order by date desc limit 0, 96;'" \
    using 1:2 index 0 title "ext pressure (Pa)" with lines lt rgb '#867088' lw 2


# light

set tmarg 0
set bmarg 0
set lmarg 7
set rmarg 15
set size 1,0.1
set origin 0.0,0.205

#set ylabel 'light (8b)'
set ytics 64
set yrange [0:256]

plot "< sqlite3 /var/lib/ss_daemon/data_tmp.db 'select date, l_ext from sensors order by date desc limit 0, 96;'" \
    using 1:2 index 0 title "ext light (8bit ADC)" with lines lt rgb '#ef7d04' lw 2

# show timestamp in the bottom
set time

# geiger

set tmarg 0
set bmarg 0
set lmarg 7
set rmarg 15
set size 1,0.1
set origin 0.0,0.07

#set ylabel 'dosimeter (cpm)'
set ytics 10
set yrange [0:40]

plot "< sqlite3 /var/lib/ss_daemon/data_tmp.db 'select date, counts from sensors order by date desc limit 0, 96;'" \
    using 1:2 index 0 title "dosimeter (cpm)" with lines lt rgb '#ee0000' lw 2

