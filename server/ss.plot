
set terminal png 
set output 'ss_th.png' 

set timefmt "%Y-%m-%dT%H:%M:%S"
set datafile separator "|"

set xdata time
set xlabel 'time'
#set xrange ['2011-11-25T13:01:08':'2011-11-26T13:01:08']
set xtics 7200
set format x '%H'

set ylabel 'temperature (dC)'
#set ytics nomirror
set ytics 2
set yrange [-10:40]

set y2label 'humidity (%RH)'
set y2tics 8
set y2range [0:100]

set grid
set palette model RGB
set palette defined ( 0 '#00FFFF', 1 '#DC143C' )
set cbrange [-10:10]

plot "< sqlite3 /var/lib/ss_daemon/data.db 'select date, t_ext, t_int, td_ext, h_ext from sensors order by date desc limit 0, 96;'" \
    using 1:2:2 index 0 title "ext temperature (dC)" with lines palette lw 2, \
    "" using 1:3 title "int temperature (dC)" with lines lt rgb '#b22222' lw 1, \
    "" using 1:4 title "ext dew temperature (dC)" with lines lt rgb '#008080' lw 1, \
    "" using 1:5 title "ext humidity (%RH)" axes x1y2 with lines lt rgb '#6a5acd' lw 2

