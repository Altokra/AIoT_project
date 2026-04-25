// pages/Huawei_IOT.js
// 智能图书馆小程序
// 功能：传感器监控 + RGB控制 + 图书注册查询

var Temp = null;
var Phot = null;
var RED = '';
var GREEN = '';
var BLUE = '';
var RGBtemp = [0, 0, 0, 0, 0];
var autoRefreshTimer = null;
var lastBookUid = wx.getStorageSync('lastBookUid') || '';  // 持久化，避免重复调用 API

// ============== 华为云连接参数 ==============
const domainname = 'altokra';
const username = 'alto';
const password = 'wwdx,wztx121';
const projectId = '08250adf11f8442caf21bcee040d6c05';
const deviceId = '69e97a34e094d61592351ea4_pLib';  // 智能图书馆设备
const iamhttps = 'iam.cn-east-3.myhuaweicloud.com';
const iotdahttps = 'f5644079de.st1.iotda-app.cn-east-3.myhuaweicloud.com';

// ============== 图书查询 API 参数 ==============
const BOOK_API_KEY = 'ae1718d4587744b0b79f940fbef69e77';
const BOOK_API_URL = 'https://data.isbn.work/openApi/getInfoByIsbn';

Page({
    // ============== 页面数据 ==============
    data: {
        result: '等待订阅，请点击订阅按钮',
        temperature: '',
        photores: '',
        // 图书相关
        bookUid: '',
        bookIsbn: '',
        bookStatus: '',
        bookTitle: '',
        bookAuthor: '',
        bookPublisher: '',
        bookYear: '',
        bookQueryStatus: 'idle',  // idle / querying / success / failed / none
        bookRegisterEvent: false,  // 是否有新的注册事件需要处理
    },

    // ============== 按钮事件 ==============
    touchBtn_subTopic: function() {
        console.log("订阅按钮按下");
        this.setData({ result: '订阅按钮按下' });
        this.gettoken();
    },

    touchBtn_getshadow: function() {
        console.log("获取设备影子按钮按下");
        this.setData({ result: '正在获取设备影子...' });
        this.getshadow();
    },

    touchBtn_setCommand: function() {
        console.log("设备命令下发按钮按下");
        this.setData({ result: '正在下发 RGB 命令...' });
        this.setCommand(1);
    },

    // 手动查询图书按钮
    touchBtn_queryBook: function() {
        var isbn = this.data.bookIsbn;
        if (isbn && isbn.length > 0) {
            this.lookupBookByISBN(isbn);
        } else {
            wx.showToast({ title: '暂无 ISBN 可查询', icon: 'none' });
        }
    },

    // 手动下发图书信息到设备
    touchBtn_sendBookInfo: function() {
        var bookInfo = {
            isbn: this.data.bookIsbn,
            title: this.data.bookTitle,
            author: this.data.bookAuthor,
            publisher: this.data.bookPublisher,
            year: this.data.bookYear
        };
        if (bookInfo.title) {
            this.sendBookInfoCommand(bookInfo);
        } else {
            wx.showToast({ title: '请先查询书籍信息', icon: 'none' });
        }
    },

    // ============== RGB 滑动条 ==============
    slider1change: function(e) {
        RGBtemp[1] = e.detail.value;
    },
    slider2change: function(e) {
        RGBtemp[2] = e.detail.value;
    },
    slider3change: function(e) {
        RGBtemp[3] = e.detail.value;
    },
    slider4change: function(e) {
        RGBtemp[4] = e.detail.value;
    },

    // ============== 开关灯 ==============
    onClickOpen: function() {
        this.setData({ result: '开灯命令下发中...' });
        this.setCommand(1);
    },
    onClickOff: function() {
        this.setData({ result: '关灯命令下发中...' });
        this.setCommand(0);
    },

    // ============== 获取 Token ==============
    gettoken: function() {
        var that = this;
        wx.request({
            url: `https://${iamhttps}/v3/auth/tokens`,
            data: `{"auth": { "identity": {"methods": ["password"],"password": {"user": {"name": "${username}","password": "${password}","domain": {"name": "${domainname}"}}}},"scope": {"project": {"name": "cn-north-4"}}}}`,
            timeout: 10000,
            method: 'POST',
            header: { 'content-type': 'application/json' },
            success: function(res) {
                console.log("获取token成功");
                console.log(res);
                var token = JSON.stringify(res.header['X-Subject-Token']);
                token = token.replaceAll("\"", "");
                console.log("获取token=\n" + token);
                wx.setStorageSync('token', token);
                that.setData({ result: 'Token 获取成功，5秒后自动刷新设备影子' });
                // 获取 token 后立即获取一次设备影子
                setTimeout(() => that.getshadow(), 500);
            },
            fail: function() {
                that.setData({ result: 'Token 获取失败，请检查网络' });
            }
        });
    },

    // ============== 获取设备影子 ==============
    getshadow: function() {
        var that = this;
        var token = wx.getStorageSync('token');
        if (!token) {
            console.log("无 Token，先获取 Token");
            return;
        }

        wx.request({
            url: `https://${iotdahttps}/v5/iot/${projectId}/devices/${deviceId}/shadow`,
            data: '',
            method: 'GET',
            header: { 'content-type': 'application/json', 'X-Auth-Token': token },
            success: function(res) {
                if (!res.data.shadow || !res.data.shadow[0]) {
                    console.log("设备影子为空");
                    return;
                }
                var props = res.data.shadow[0].reported.properties;

                // 更新传感器数据
                Temp = props.Temperature;
                Phot = props.Photores;
                RED = props.RED;
                GREEN = props.GREEN;
                BLUE = props.BLUE;

                // 提取图书数据
                var bookUid = props.book_uid || '';
                var bookIsbn = props.book_isbn || '';
                var bookStatus = props.book_status || '';
                var statusText = '温度' + Temp + '℃ 光照' + Phot;

                // 如果有图书数据，添加到状态文本
                if (bookIsbn) {
                    statusText += ' | 书:' + bookIsbn + ' [' + bookStatus + ']';
                }

                that.setData({
                    result: statusText,
                    temperature: Temp ? Temp + '℃' : '--',
                    photores: Phot || '--',
                    bookUid: bookUid,
                    bookIsbn: bookIsbn,
                    bookStatus: bookStatus
                });

                // 检测到新的图书注册事件 → 自动查询书籍信息
                if (bookUid && bookUid !== lastBookUid && bookStatus === 'registered') {
                    console.log("检测到新书注册，ISBN:", bookIsbn);
                    lastBookUid = bookUid;
                    wx.setStorageSync('lastBookUid', bookUid);  // 持久化，防止重启后重复查询
                    that.lookupBookByISBN(bookIsbn);
                }

                // 检测到图书借出/归还事件
                if (bookStatus === 'borrowed' || bookStatus === 'returned') {
                    wx.showToast({
                        title: '图书' + (bookStatus === 'borrowed' ? '已借出' : '已归还'),
                        icon: 'none',
                        duration: 2000
                    });
                }
            },
            fail: function() {
                console.log("获取设备影子失败");
            }
        });
    },

    // ============== 查询图书信息（调用 data.isbn.work API）==============
    lookupBookByISBN: function(isbn) {
        if (!isbn) return;

        var that = this;
        that.setData({
            bookQueryStatus: 'querying',
            bookTitle: '查询中...',
            bookAuthor: '',
            bookPublisher: '',
            bookYear: '',
            result: '正在查询书籍: ' + isbn
        });

        wx.request({
            url: BOOK_API_URL,
            data: { isbn: isbn, appKey: BOOK_API_KEY },
            method: 'GET',
            success: function(res) {
                console.log("图书 API 返回:", res.data);

                if (res.data.code === 0 && res.data.success && res.data.data) {
                    var book = res.data.data;
                    // 提取年份
                    var year = 0;
                    var pressDate = book.pressDate || '';
                    var yearMatch = pressDate.match(/\d{4}/);
                    if (yearMatch) year = parseInt(yearMatch[0]);

                    that.setData({
                        bookQueryStatus: 'success',
                        bookTitle: book.bookName || '未知书名',
                        bookAuthor: book.author || '未知作者',
                        bookPublisher: book.press || '未知出版社',
                        bookYear: year || '',
                        result: '查询成功: ' + (book.bookName || isbn)
                    });

                    // 自动下发书籍信息到 ESP32
                    setTimeout(() => {
                        that.sendBookInfoCommand({
                            isbn: isbn,
                            title: book.bookName || '',
                            author: book.author || '',
                            publisher: book.press || '',
                            year: year
                        });
                    }, 500);

                } else {
                    that.setData({
                        bookQueryStatus: 'failed',
                        bookTitle: '未找到该书籍',
                        bookAuthor: '',
                        bookPublisher: '',
                        bookYear: '',
                        result: '未找到书籍: ' + isbn
                    });
                }
            },
            fail: function(err) {
                console.error("图书 API 请求失败:", err);
                that.setData({
                    bookQueryStatus: 'failed',
                    bookTitle: '查询失败',
                    result: '网络错误，请重试'
                });
            }
        });
    },

    // ============== 下发图书信息命令到 ESP32 ==============
    sendBookInfoCommand: function(bookInfo) {
        var that = this;
        var token = wx.getStorageSync('token');
        if (!token) {
            console.log("无 Token，无法下发命令");
            return;
        }

        wx.request({
            url: `https://${iotdahttps}/v5/iot/${projectId}/devices/${deviceId}/commands`,
            data: JSON.stringify({
                service_id: "Env",
                command_name: "deliver_book_info",
                paras: {
                    isbn: bookInfo.isbn,
                    title: bookInfo.title,
                    author: bookInfo.author,
                    publisher: bookInfo.publisher,
                    year: bookInfo.year
                }
            }),
            method: 'POST',
            header: { 'content-type': 'application/json', 'X-Auth-Token': token },
            success: function(res) {
                console.log("图书信息下发成功:", res);
                wx.showToast({ title: '已下发到设备', icon: 'success', duration: 1500 });
            },
            fail: function(err) {
                console.error("图书信息下发失败:", err);
                wx.showToast({ title: '下发失败', icon: 'none' });
            }
        });
    },

    // ============== 下发 RGB 命令 ==============
    setCommand: function(key) {
        var that = this;
        var token = wx.getStorageSync('token');
        if (!token) {
            that.setData({ result: '请先点击订阅按钮获取 Token' });
            return;
        }

        wx.request({
            url: `https://${iotdahttps}/v5/iot/${projectId}/devices/${deviceId}/commands`,
            data: JSON.stringify({
                service_id: "Env",
                command_name: "LEDControl",
                paras: {
                    RED: RGBtemp[1] || 0,
                    GREEN: RGBtemp[3] || 0,
                    BLUE: RGBtemp[2] || 0,
                    Switch: key ? true : false
                }
            }),
            method: 'POST',
            header: { 'content-type': 'application/json', 'X-Auth-Token': token },
            success: function(res) {
                console.log("RGB 命令下发成功:", res);
                that.setData({ result: '命令下发成功' });
            },
            fail: function(err) {
                console.error("RGB 命令下发失败:", err);
                that.setData({ result: '命令下发失败，请重试' });
            }
        });
    },

    // ============== 生命周期 ==============
    onLoad: function(options) {
        console.log("页面加载");
    },

    onShow: function() {
        // 页面显示时启动自动刷新
        if (!autoRefreshTimer) {
            autoRefreshTimer = setInterval(() => {
                this.getshadow();
            }, 5000);
        }
    },

    onHide: function() {
        if (autoRefreshTimer) {
            clearInterval(autoRefreshTimer);
            autoRefreshTimer = null;
        }
    },

    onUnload: function() {
        if (autoRefreshTimer) {
            clearInterval(autoRefreshTimer);
            autoRefreshTimer = null;
        }
    }
});
