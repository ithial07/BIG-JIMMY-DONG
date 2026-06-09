#include <QtWidgets>
#include <QtNetwork>

struct SafeRoom { int id=0, area=0, x=0, y=0, z=0, mudId=0; QString name; QMap<QString,int> exits; };

class SafeMap : public QWidget {
public:
    SafeMap(QWidget* parent=nullptr): QWidget(parent) { setMinimumSize(420,320); setMouseTracking(true); }
    void load() {
        QFile f(QStringLiteral(":/resources/ardabest.json"));
        if (!f.open(QIODevice::ReadOnly)) return;
        auto doc=QJsonDocument::fromJson(f.readAll());
        auto root=doc.object();
        for (auto v: root.value("rooms").toArray()) {
            auto o=v.toObject(); SafeRoom r; r.id=o.value("id").toInt(); r.area=o.value("area").toInt(); r.x=o.value("x").toInt(); r.y=o.value("y").toInt(); r.z=o.value("z").toInt(); r.name=o.value("name").toString();
            QRegularExpression re(QStringLiteral("\\(#(\\d+)\\)")); auto m=re.match(r.name); if(m.hasMatch()) { r.mudId=m.captured(1).toInt(); mudToMapper[r.mudId]=r.id; }
            auto ex=o.value("exits").toObject(); for (auto it=ex.begin(); it!=ex.end(); ++it) r.exits[it.key()]=it.value().toInt();
            rooms[r.id]=r;
        }
        if (rooms.contains(12970)) setCurrent(12970); else if(!rooms.isEmpty()) setCurrent(rooms.firstKey());
    }
    void setCurrentByMud(int mud) { if(mudToMapper.contains(mud)) setCurrent(mudToMapper[mud]); }
    void setCurrent(int id) { if(!rooms.contains(id)) return; current=id; area=rooms[id].area; z=rooms[id].z; update(); }
    int currentMud() const { if(!rooms.contains(current)) return 0; return rooms[current].mudId; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.fillRect(rect(), QColor(0,7,0)); p.setRenderHint(QPainter::Antialiasing);
        QPen grid(QColor(0,60,25)); for(int x=0;x<width();x+=22) p.drawLine(x,0,x,height()); for(int y=0;y<height();y+=22) p.drawLine(0,y,width(),y);
        if(!rooms.contains(current)) return; auto cr=rooms[current]; double scale=18.0; QPointF center(width()/2.0,height()/2.0); auto pos=[&](const SafeRoom& r){ return QPointF(center.x()+(r.x-cr.x)*scale, center.y()-(r.y-cr.y)*scale); };
        QSet<int> drawn;
        QPen line(QColor(0,255,70)); line.setWidth(2); p.setPen(line);
        for(auto it=rooms.begin(); it!=rooms.end(); ++it){ const auto& r=it.value(); if(r.area!=area||r.z!=z) continue; QPointF a=pos(r); if(!rect().adjusted(-50,-50,50,50).contains(a.toPoint())) continue; for(int to: r.exits){ if(!rooms.contains(to)||drawn.contains(to*200000+r.id)) continue; auto rr=rooms[to]; if(rr.area==area && rr.z==z){ p.drawLine(a,pos(rr)); drawn.insert(r.id*200000+to); } } }
        for(auto it=rooms.begin(); it!=rooms.end(); ++it){ const auto& r=it.value(); if(r.area!=area||r.z!=z) continue; QPointF pp=pos(r); if(!rect().adjusted(-50,-50,50,50).contains(pp.toPoint())) continue; QRectF box(pp.x()-6,pp.y()-6,12,12); p.setPen(QPen(QColor(0,255,70),2)); p.setBrush(QColor(20,20,20)); p.drawRect(box); }
        QPointF cp=pos(cr); p.setPen(QPen(QColor(0,255,70),2)); p.setBrush(QColor(0,255,70,60)); p.drawEllipse(cp,18,18); p.drawEllipse(cp,11,11); p.setBrush(QColor(0,255,70)); p.drawEllipse(cp,6,6); p.setPen(QColor(0,255,70)); p.drawText(cp+QPointF(16,-10), QStringLiteral("YOU ARE HERE #%1").arg(cr.mudId>0?cr.mudId:cr.id));
    }
private:
    QMap<int,SafeRoom> rooms; QMap<int,int> mudToMapper; int current=0, area=6, z=0;
};

class SafeWin: public QMainWindow{
public:
    SafeWin(){ setWindowTitle("ArdaBest MUD Client - SAFE MODE"); resize(1200,760); QWidget* root=new QWidget; auto* lay=new QVBoxLayout(root); auto* top=new QHBoxLayout; host=new QLineEdit("rotsmud.org"); port=new QLineEdit("3791"); QPushButton* con=new QPushButton("Connect"); QPushButton* bg=new QPushButton("Background..."); color=new QComboBox; for(QString s: {"White","Bright Green","Green","Light Green","Bright Blue","Red","Yellow","Purple"}) color->addItem(s); top->addWidget(new QLabel("Host:"));top->addWidget(host);top->addWidget(new QLabel("Port:"));top->addWidget(port);top->addWidget(con);top->addWidget(bg);top->addWidget(new QLabel("Text:"));top->addWidget(color);top->addStretch(); lay->addLayout(top); auto* split=new QSplitter; term=new QTextEdit; term->setReadOnly(true); term->setStyleSheet("QTextEdit{background:black;color:#eeeeee;font-family:Consolas,monospace;font-size:10pt;}"); map=new SafeMap; map->load(); split->addWidget(term); split->addWidget(map); split->setStretchFactor(0,3); split->setStretchFactor(1,2); lay->addWidget(split,1); input=new QLineEdit; lay->addWidget(input); setCentralWidget(root); sock=new QTcpSocket(this); connect(con,&QPushButton::clicked,this,[=]{ if(sock->state()==QTcpSocket::ConnectedState){sock->disconnectFromHost(); return;} sock->connectToHost(host->text(),port->text().toUShort()); }); connect(sock,&QTcpSocket::connected,this,[=]{term->append("[SAFE] Connected."); con->setText("Disconnect");}); connect(sock,&QTcpSocket::disconnected,this,[=]{term->append("[SAFE] Disconnected."); con->setText("Connect");}); connect(sock,&QTcpSocket::readyRead,this,[=]{QString t=QString::fromLocal8Bit(sock->readAll()); term->moveCursor(QTextCursor::End); term->insertPlainText(t); QRegularExpression re("\\(#(\\d+)\\).*Exits\\s*(?:are)?\\s*:", QRegularExpression::CaseInsensitiveOption); auto m=re.match(t); if(m.hasMatch()) map->setCurrentByMud(m.captured(1).toInt());}); connect(input,&QLineEdit::returnPressed,this,[=]{QString s=input->text(); input->clear(); if(s=="clear"){term->clear();return;} if(sock->state()==QTcpSocket::ConnectedState) sock->write((s+"\n").toLocal8Bit());}); connect(bg,&QPushButton::clicked,this,[=]{QString f=QFileDialog::getOpenFileName(this,"Background",QDir::homePath(),"Images (*.png *.jpg *.jpeg *.bmp *.webp)"); if(f.isEmpty()) return; term->setStyleSheet(QString("QTextEdit{background-image:url(%1); background-attachment: fixed; color:#eeeeee; font-family:Consolas,monospace; font-size:10pt;}").arg(f.replace('\\','/')));}); connect(color,&QComboBox::currentTextChanged,this,[=](QString c){QColor q=Qt::white; if(c=="Bright Green") q=QColor(0,255,70); else if(c=="Green") q=QColor(0,190,60); else if(c=="Light Green") q=QColor(135,255,150); else if(c=="Bright Blue") q=QColor(70,150,255); else if(c=="Red") q=Qt::red; else if(c=="Yellow") q=Qt::yellow; else if(c=="Purple") q=QColor(180,80,255); term->setTextColor(q);}); term->append("[SAFE MODE] This fallback client avoids all experimental detachable-map/profile code. It uses rotsmud.org:3791 and the ARDABEST map."); }
private: QTextEdit* term; QLineEdit *input,*host,*port; QComboBox* color; SafeMap* map; QTcpSocket* sock; };

int main(int argc, char** argv){ qputenv("QT_OPENGL","software"); QApplication app(argc,argv); SafeWin w; w.show(); return app.exec(); }
