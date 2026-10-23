// Small shared drafts for project operations. GPL-2.0-or-later.
#pragma once
#include "ResearchAppearance.h"
#include "ResearchSettings.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QScreen>
namespace Research {
inline void configureWorkspaceText(QPlainTextEdit *edit){edit->setFont(interfaceFont());edit->setFrameShape(QFrame::NoFrame);edit->setProperty("researchWhiteSurface",true);edit->document()->setDocumentMargin(8);}

inline void fitWorkspaceDialog(QDialog *d,int width=840,int height=570){const auto available=d->screen()->availableGeometry().size()-QSize(32,48);d->resize(QSize(qMin(width,qMax(400,d->parentWidget()?d->parentWidget()->width()-24:width)),height).boundedTo(available));}
inline QDialogButtonBox *workspaceFooter(QDialog *d,QVBoxLayout *l,const QString &caption){auto *b=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,d);b->button(QDialogButtonBox::Ok)->setText(caption);b->button(QDialogButtonBox::Cancel)->setText(words("取消","Cancel"));for(auto *p:b->buttons())configureButton(qobject_cast<QPushButton*>(p));QObject::connect(b,&QDialogButtonBox::rejected,d,&QDialog::reject);l->addWidget(b);return b;}
inline QString workspaceInput(QWidget *parent,const QString &title,const QString &label,const QString &initial={},bool *accepted=nullptr,bool trim=true){MaterialDialog d(parent);d.setObjectName(QStringLiteral("researchWorkspaceInputDialog"));configureSettingsSurface(&d);d.setWindowTitle(title);auto *l=new QVBoxLayout(&d);auto *hint=new QLabel(label,&d);hint->setWordWrap(true);l->addWidget(hint);auto *field=new QLineEdit(initial,&d);field->setObjectName(QStringLiteral("researchWorkspaceInput"));configureField(field);l->addWidget(field);auto *b=workspaceFooter(&d,l,words("继续","Continue"));QObject::connect(b,&QDialogButtonBox::accepted,&d,&QDialog::accept);fitWorkspaceDialog(&d,520,150);field->selectAll();const bool ok=d.exec()==QDialog::Accepted;if(accepted)*accepted=ok;return ok?(trim?field->text().trimmed():field->text()):QString();}
inline bool workspaceConfirm(QWidget *parent,const QString &title,const QString &text,const QString &action){MaterialDialog d(parent);d.setObjectName(QStringLiteral("researchWorkspaceConfirmDialog"));configureSettingsSurface(&d);d.setWindowTitle(title);auto *l=new QVBoxLayout(&d);auto *detail=new QPlainTextEdit(&d);configureWorkspaceText(detail);detail->setReadOnly(true);detail->setPlainText(text);l->addWidget(detail);auto *b=workspaceFooter(&d,l,action);QObject::connect(b,&QDialogButtonBox::accepted,&d,&QDialog::accept);fitWorkspaceDialog(&d,680,400);return d.exec()==QDialog::Accepted;}
}
