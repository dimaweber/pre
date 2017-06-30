#include <QApplication>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDateTime>
#include <QTime>
#include <QVariant>
#include <QChart>
#include <QChartView>
#include <QCandlestickSeries>
#include <QCandlestickSet>
#include <QBarSeries>
#include <QBarSet>
#include <QDateTimeAxis>
#include <QBoxLayout>
#include <QValueAxis>
#include <iostream>
#include <iomanip>

typedef qreal Rate;

struct Period
{
    Rate open;
    Rate close;
    Rate min;
    Rate max;
    double vol;
    double cur_vol;
    QDateTime periodStart;
    QDateTime periodEnd;

    double body_size() const
    {
        return qAbs(open-close);
    }

    double upper_tail_size() const
    {
        return qAbs(max - qMax(open,close));
    }

    double lower_tail_size() const
    {
        return qAbs(min - qMin(open,close));
    }

    bool isRise() const
    {
        return close > open;
    }
};

struct VolumeHistoryRecord
{
    QDateTime time;
    double volume_24h;
    double moment_volume;
};

using namespace QtCharts;


#define DAYS 7
#define granularity_hours 4
#define granularity_minutes 0

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // GET DATA
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "conn");
    db.setHostName("192.168.10.4");
    db.setDatabaseName("trade");
    db.setUserName("trader");
    db.setPassword("traderpassword");

    if (!db.open())
    {
        std::cerr << qPrintable(db.lastError().text()) << std::endl;
    }

    QSqlQuery query(db);
    QString sql  = "select time,last_rate,currency_volume from rates where currency='usd' and goods='btc' and time >= now() - interval :int day  order by time asc";
    if (!query.prepare(sql))
    {
        std::cerr << qPrintable(query.lastError().text()) << std::endl;
    }
    query.bindValue(":int", DAYS);

    if (!query.exec())
    {
        std::cerr << qPrintable(query.lastError().text()) << std::endl;
    }

    // GROUP DATA
    QDateTime periodStart = QDateTime::currentDateTime().addDays(-DAYS - 2);
    QTime granularity (granularity_hours, granularity_minutes, 0);
    int granularitySec = QTime(0,0,0).secsTo(granularity);

    Period* period = nullptr;
    QList<Period*> periods;
    QVector<VolumeHistoryRecord> volumeHistory;

    double prevVolume = 0;
    int n = 0;

    while (query.next())
    {
        QDateTime time = query.value(0).toDateTime();
        Rate rate = query.value(1).toDouble();
        double volume = query.value(2).toDouble();

        VolumeHistoryRecord rec;
        rec.time = time;
        rec.volume_24h = volume;
        rec.moment_volume = 0;
        QDateTime one_day_before = time.addDays(-1);
        if (volumeHistory.size() > 0 && one_day_before >= volumeHistory.at(0).time)
        {
            int k = n - (24*60*60 / 10);
            if ( k >= 0)
            {
                while (volumeHistory[k].time > one_day_before)
                    k--;
                k++;
                while (volumeHistory[k].time < one_day_before)
                    k++;
                rec.moment_volume = rec.volume_24h - prevVolume + volumeHistory[k].moment_volume;
                if (rec.moment_volume < 0)
                {
                    if (volumeHistory[k].moment_volume == 0)
                        volumeHistory[k].moment_volume = -rec.moment_volume;
                    rec.moment_volume = 0;
                }
                std::cout << k << " " << qPrintable(volumeHistory[k].time.toString(Qt::ISODate)) <<  "  " << volumeHistory[k].volume_24h << "    " << volumeHistory[k].moment_volume << std::endl;
            }
        }
        std::cout << n << " "<< qPrintable(rec.time.toString(Qt::ISODate)) <<  "  " << rec.volume_24h << "    " << rec.moment_volume << std::endl;
        n++;
        volumeHistory.append(rec);

        if (period)
        {
            period->periodEnd = time;
            period->close = rate;
            period->max = qMax(period->max, rate);
            period->min = qMin(period->min, rate);
            period->vol += rec.moment_volume;
        }

        if (periodStart.secsTo(time) > granularitySec)
        {

            // new period
            period = new Period;
            periods.append(period);

            period->periodStart = time;
            period->periodEnd = time;
            period->open = rate;
            period->max = rate;
            period->min = rate;
            period->vol = 0;

            periodStart = time;
        }
        prevVolume = volume;
    }

    // DISPLAY
    QStringList categories;
    QCandlestickSeries series;
    QCandlestickSet* candle = nullptr;
    QBarSeries bars;
    QBarSet volume("Volume");

    double maxVolume=0;
    double minVolume = 1e20;
    double minRate = 1e20;
    double maxRate  = 0;
    for (Period* period: periods)
    {
        std::cout << qPrintable(period->periodStart.toString(Qt::ISODate)) << "     "
                  << qPrintable(period->periodEnd.toString(Qt::ISODate)) << "     "
                  << std::setw(8) << qPrintable(QString::number(period->upper_tail_size(), 'f', 3)) << "   "
                  << std::setw(8) << qPrintable(QString::number(period->body_size(), 'f', 3)) << "   "
                  << std::setw(8) << qPrintable(QString::number(period->lower_tail_size(), 'f', 3)) << "   "
                  << std::setw(8) << period->isRise()
                  << std::endl;

        candle = new QCandlestickSet(period->open, period->max, period->min, period->close, period->periodStart.toMSecsSinceEpoch());
        categories << period->periodStart.toString("dd HH:mm");
        series.append(candle);
        volume.append(period->vol);
        minRate = qMin(period->min, minRate);
        maxRate = qMax(period->max, maxRate);
        maxVolume = qMax(period->vol, maxVolume);
        minVolume = qMin(period->vol, minVolume);
    }

    series.setName("BTC / USD");
    series.setIncreasingColor(QColor(Qt::green));
    series.setDecreasingColor(QColor(Qt::red));

    bars.append(&volume);
    volume.setBrush(QBrush(QColor(34, 45, 178, 64)));

    QWidget* widget = new QWidget;
    QChart chart;
    chart.addSeries(&series);
    chart.addSeries(&bars);
    chart.setTitle("BTC / USD");
    chart.setAnimationOptions(QChart::SeriesAnimations);

    QChartView *chartView = new QChartView(widget);
    chartView->setRenderHint(QPainter::Antialiasing);

    chartView->setChart(&chart);

    chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QDateTimeAxis *axisX = new QDateTimeAxis();
    axisX->setTickCount(10);
    axisX->setFormat("MMM dd HH:mm");
    axisX->setTitleText("Date");
    chart.setAxisX(axisX, &series);
    //chart.setAxisX(axisX, &bars);
    axisX->setLabelsAngle(60);

    QValueAxis *axisY = new QValueAxis();
    chart.setAxisY(axisY, &series);
    axisY->setRange(0.99 * minRate, 1.01 * maxRate);

    QValueAxis *axisY1 = new QValueAxis();
    chart.setAxisY(axisY1, &bars);
    axisY1->setRange(minVolume, maxVolume*4);

    widget->setLayout(new QVBoxLayout);
    widget->layout()->addWidget(chartView);

    widget->setMinimumSize(1024, 768);
    widget->show();

    return a.exec();
}
