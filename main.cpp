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

using namespace QtCharts;


#define DAYS 1
#define granularity_hours 0
#define granularity_minutes 15

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // GET DATA
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "conn");
    db.setHostName("mysql-master.vm.dweber.lan");
    db.setDatabaseName("rates");
    db.setUserName("rates");
    db.setPassword("rates");

    if (!db.open())
    {
        std::cerr << qPrintable(db.lastError().text()) << std::endl;
    }

    QSqlQuery query(db);
    QTime granularity (granularity_hours, granularity_minutes, 0);
    int granularitySec = QTime(0,0,0).secsTo(granularity);
    int s = QDateTime::currentDateTime().toSecsSinceEpoch();
    QDateTime periodStart = QDateTime::fromSecsSinceEpoch(s - s % granularitySec + granularitySec).addDays(-DAYS);
    QString sql  = "select time,rate,amount from rates where exchange='btc-e' and pair='btc_usd'  and time >= :start order by time asc";

    if (!query.prepare(sql))
    {
        std::cerr << qPrintable(query.lastError().text()) << std::endl;
    }
    query.bindValue(":start", periodStart);

    if (!query.exec())
    {
        std::cerr << qPrintable(query.lastError().text()) << std::endl;
    }

    // GROUP DATA
    Period* period = nullptr;
    QList<Period*> periods;

    while (query.next())
    {
        QDateTime time = query.value(0).toDateTime();
        Rate rate = query.value(1).toDouble();
        double volume = query.value(2).toDouble();

        if (period)
        {
            period->periodEnd = time;
            period->close = rate;
            period->max = qMax(period->max, rate);
            period->min = qMin(period->min, rate);
            period->vol += volume;
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
