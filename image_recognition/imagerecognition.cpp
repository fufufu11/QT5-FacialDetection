#include "imagerecognition.h"
#include "ui_imagerecognition.h"

#include "worker.h"
#include <QCamera>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QCameraInfo>
ImageRecognition::ImageRecognition(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ImageRecognition)
{
    ui->setupUi(this);
    //显示可用的摄像头
    camerasInfo = QCameraInfo::availableCameras();
    for(const QCameraInfo &cameraInfo : camerasInfo)
    {
     //   qDebug() << cameraInfo.description();
        ui->comboBox->addItem(cameraInfo.description());
    }
    connect(ui->comboBox,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&ImageRecognition::pickCamera);
    //调用摄像头设备
    camera = new QCamera();
    finder = new QCameraViewfinder();
    imageCapture = new QCameraImageCapture(camera);
    camera->setViewfinder(finder);
    camera->setCaptureMode(QCamera::CaptureStillImage);
    imageCapture->setCaptureDestination(QCameraImageCapture::CaptureToBuffer);
    connect(imageCapture,&QCameraImageCapture::imageCaptured,this,&ImageRecognition::showCamera);
    connect(ui->pushButton,&QPushButton::clicked,this,&ImageRecognition::controlWorker);
    camera->start();

    //利用定时器不断刷新拍照界面
    refreshTimer = new QTimer();
    connect(refreshTimer,&QTimer::timeout,this,&ImageRecognition::takePicture);
    refreshTimer->start(20);

    //利用定时器不断进行人脸识别请求
    netTimer = new QTimer();
    connect(netTimer,&QTimer::timeout,this,&ImageRecognition::controlWorker);

    //进行界面布局
    this->resize(1200,700);
    finder->setMinimumSize(600,400);
    QVBoxLayout* vbox = new QVBoxLayout();
    QVBoxLayout* vbox_2 = new QVBoxLayout();
    vbox->addWidget(ui->label);
    vbox->addWidget(ui->pushButton);
    vbox_2->addWidget(ui->comboBox);
    vbox_2->addWidget(finder);
    vbox_2->addWidget(ui->textBrowser);
    QHBoxLayout* hbox = new QHBoxLayout(this);
    hbox->addLayout(vbox);
    hbox->addLayout(vbox_2);
    this->setLayout(hbox);

    tokenManager = new QNetworkAccessManager(this);
    connect(tokenManager,&QNetworkAccessManager::finished,this,&ImageRecognition::tokenReply);

    imgManager = new QNetworkAccessManager(this);
    connect(imgManager,&QNetworkAccessManager::finished,this,&ImageRecognition::imgReply);
    qDebug() << tokenManager->supportedSchemes();
    //url参数设置
    QUrl url("https://aip.baidubce.com/oauth/2.0/token");
    QUrlQuery query;
    query.addQueryItem("grant_type","client_credentials");
   // query.addQueryItem("client_id","P2b5h1vg1s3pjSuRmD3UKcVI");
   // query.addQueryItem("client_secret","Lil37xbKf53EgjdzSSU5wNdhpuJRy53B");

    query.addQueryItem("client_id","oQXBR1aW4BrlvqrvMpCAkyCd");
    query.addQueryItem("client_secret","BxjQcEpqiKDuUR19e1vv3F4CFTBQgPpj");
    url.setQuery(query);
    qDebug() << url;

    //SSL支持
    if(QSslSocket::supportsSsl())
    {
        qDebug() << "支持SSL";
    }
    else
    {
        qDebug() << "不支持SSL";
    }
    //SSL配置
    sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::QueryPeer);
    sslConfig.setProtocol(QSsl::TlsV1_2);
    //组装请求
    QNetworkRequest req;
    req.setUrl(url);
    req.setSslConfiguration(sslConfig);
    //发送get请求
    tokenManager->get(req);
}

ImageRecognition::~ImageRecognition()
{
    delete ui;
}

void ImageRecognition::showCamera(int id,QImage preview)
{
    Q_UNUSED(id);
    img = preview;
    //绘制人脸框
    QPen pen(Qt::white);
    pen.setWidth(5);
    QPainter p(&preview);
    p.setPen(pen);
    p.drawRect(faceLeft,faceTop,faceWidth,faceHeight);
    QFont font;
    font.setPixelSize(50);
    p.setFont(font);
    p.drawText(faceLeft+faceWidth+5,faceTop,QString("年龄：").append(QString::number(age)));
    p.drawText(faceLeft+faceWidth+5,faceTop+50,QString("性别：").append(gender=="male"?"男":"女"));
    p.drawText(faceLeft+faceWidth+5,faceTop+100,QString("情绪：").append(emotion));
    p.drawText(faceLeft+faceWidth+5,faceTop+150,QString("颜值：").append(QString::number(beauty)));
    p.drawText(faceLeft+faceWidth+5,faceTop+200,QString("口罩：").append(mask==1?"佩戴":"未佩戴"));
    p.drawText(faceLeft+faceWidth+5,faceTop+250,QString("眼镜：").append(glasses=="none"?"未佩戴":glasses=="common"?"普通眼镜":"墨镜"));
    p.drawText(faceLeft+faceWidth+5,faceTop+300,QString("真人可能性：").append(QString::number(liveness)));
    ui->label->setPixmap(QPixmap::fromImage(preview));
}

void ImageRecognition::takePicture()
{
    imageCapture->capture();
}

void ImageRecognition::tokenReply(QNetworkReply *reply)
{
    //错误处理
    if(reply->error() != QNetworkReply::NoError)
    {
        qDebug() << reply->errorString();
        return;
    }
    //正常应答
    const QByteArray reply_data = reply->readAll();
    //qDebug() << reply_data;

    //JSON解析
    QJsonParseError jsonErr;
    QJsonDocument doc = QJsonDocument::fromJson(reply_data,&jsonErr);
    //解析成功
    if(jsonErr.error == QJsonParseError::NoError)
    {
        QJsonObject obj = doc.object();
        if(obj.contains("access_token"))
        {
            accessToken = obj.take("access_token").toString();
        }
        ui->textBrowser->setText(accessToken);
    }
    //解析失败
    else
    {
        qDebug() << "JSON ERR:" << jsonErr.errorString();
    }
    netTimer->start(1000);
    reply->deleteLater();

}
//因为转换成 base64 编码会造成卡顿，把这部分放到线程里执行
void ImageRecognition::controlWorker()
{
    childThread = new QThread(this);
    Worker* worker = new Worker;
    worker->moveToThread(childThread);
    connect(this,&ImageRecognition::operate,worker,&Worker::doWork);
    connect(worker,&Worker::resultReady,this,&ImageRecognition::beginFaceDetect);
    connect(childThread,&QThread::finished,worker,&QObject::deleteLater);
    childThread->start();
    emit operate(img,childThread);
}

void ImageRecognition::pickCamera(int idx)
{
    qDebug() << camerasInfo.at(idx).description();
    refreshTimer->stop();
    camera->stop();
    camera = new QCamera(camerasInfo.at(idx));
    imageCapture = new QCameraImageCapture(camera);
    connect(imageCapture,&QCameraImageCapture::imageCaptured,this,&ImageRecognition::showCamera);
    imageCapture->setCaptureDestination(QCameraImageCapture::CaptureToBuffer);
    camera->start();
    refreshTimer->start();
}

void ImageRecognition::beginFaceDetect(QByteArray postData,QThread* overThread)
{
    overThread->exit();
    overThread->wait();
    //组装图像识别请求
    QUrl url("https://aip.baidubce.com/rest/2.0/face/v3/detect");
    QUrlQuery query;
    query.addQueryItem("access_token",accessToken);
    url.setQuery(query);
    //组装请求
    QNetworkRequest req;
    req.setHeader(QNetworkRequest::ContentTypeHeader,QVariant("application/json"));
    req.setUrl(url);
    req.setSslConfiguration(sslConfig);
    imgManager->post(req,postData);
}

void ImageRecognition::imgReply(QNetworkReply *reply)
{
    if(reply->error() != QNetworkReply::NoError)
    {
        qDebug() << reply->errorString();
        return;
    }
    const QByteArray replyData = reply->readAll();
    qDebug() << replyData;
    QString faceInfo = "";
    QJsonParseError jsonErr;
    QJsonDocument doc = QJsonDocument::fromJson(replyData,&jsonErr);
    if(jsonErr.error == QJsonParseError::NoError)
    {
        QJsonObject obj = doc.object();

        if(obj.contains("timestamp"))
        {
            int tmpTime = obj.take("timestamp").toInt();
            if(tmpTime < latestTime)
            {
                return;
            }
            else
            {
                latestTime = tmpTime;
            }

        }
        if(obj.contains("result"))
        {
            QJsonObject resultObj = obj.take("result").toObject();
            //取出人脸列表
            if(resultObj.contains("face_list"))
            {
                QJsonArray faceList = resultObj.take("face_list").toArray();
                //取出第一张人脸信息
                QJsonObject faceObject = faceList.at(0).toObject();
                //取出人脸定位信息
                if(faceObject.contains("location"))
                {
                    QJsonObject locObj = faceObject.take("location").toObject();
                    if(locObj.contains("left"))
                    {
                        faceLeft = locObj.take("left").toDouble();
                    }
                    if(locObj.contains("top"))
                    {
                        faceTop = locObj.take("top").toDouble();
                    }
                    if(locObj.contains("width"))
                    {
                        faceWidth = locObj.take("width").toDouble();
                    }
                    if(locObj.contains("height"))
                    {
                        faceHeight = locObj.take("height").toDouble();
                    }
                }
                //取出年龄
                if(faceObject.contains("age"))
                {
                    age = faceObject.take("age").toDouble();
                    faceInfo.append("年龄:").append(QString::number(age)).append("\r\n");
                }
                //取出性别
                if(faceObject.contains("gender"))
                {
                    QJsonObject genderObj = faceObject.take("gender").toObject();
                    if(genderObj.contains("type"))
                    {
                        gender = genderObj.take("type").toString();
                        faceInfo.append("性别:").append(gender=="male"?"男":"女").append("\r\n");
                    }
                }
                //取出情绪
                if(faceObject.contains("emotion"))
                {
                    QJsonObject emotionObj = faceObject.take("emotion").toObject();
                    if(emotionObj.contains("type"))
                    {
                        emotion = emotionObj.take("type").toString();
                        faceInfo.append("情绪:").append(emotion).append("\r\n");
                    }
                }
                //取出颜值
                if(faceObject.contains("beauty"))
                {
                    beauty = faceObject.take("beauty").toDouble();
                    faceInfo.append("颜值:").append(QString::number(beauty)).append("\r\n");
                }
                //检测是否戴口罩
                if(faceObject.contains("mask"))
                {
                    QJsonObject maskObj = faceObject.take("mask").toObject();
                    if(maskObj.contains("type"))
                    {
                        mask = maskObj.take("type").toInt();
                        faceInfo.append("是否佩戴口罩:").append(mask?"戴口罩":"未佩戴口罩").append("\r\n");
                    }
                }
                //检测是否戴眼镜
                if(faceObject.contains("glasses"))
                {
                    QJsonObject glassesObj = faceObject.take("glasses").toObject();
                    if(glassesObj.contains("type"))
                    {
                        glasses = glassesObj.take("type").toString();
                        faceInfo.append("是否戴眼镜:");
                        if(glasses == "none")
                        {
                            faceInfo.append("未佩戴眼镜").append("\r\n");
                        }
                        else if(glasses == "common")
                        {
                            faceInfo.append("佩戴普通眼镜").append("\r\n");
                        }
                        else if(glasses == "sun")
                        {
                            faceInfo.append("佩戴墨镜").append("\r\n");
                        }
                    }
                }
                //取出活体检测值
                if(faceObject.contains("liveness"))
                {
                    QJsonObject livenessObj = faceObject.take("liveness").toObject();
                    if(livenessObj.contains("livemapscore"))
                    {
                        liveness = livenessObj.take("livemapscore").toDouble();
                        faceInfo.append("是真人的可能性:").append(QString::number(liveness));
                    }
                }
                ui->textBrowser->setText(faceInfo);
            }
        }
    }
    else
    {
        qDebug() << "JSON ERR:" << jsonErr.errorString();
    }
    reply->deleteLater();
}

