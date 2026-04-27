// pages/Huawei_IOT.js
// 智能图书馆小程序 - 合并版
// 功能：传感器监控 + RGB控制 + 图书注册查询 + LED状态模拟 + 借还书时间线

var RGBtemp = [0, 0, 0, 0, 0];
var autoRefreshTimer = null;
var lastBookUid = wx.getStorageSync('lastBookUid') || '';
var shadowTimer = null;

// ============== 华为云连接参数（使用我们自己的账户）==============
const domainname = 'altokra';
const username = 'alto';
const password = 'wwdx,wztx121';
const projectId = '08250adf11f8442caf21bcee040d6c05';
const deviceId = '69e97a34e094d61592351ea4_pLib';
const iamhttps = 'iam.cn-east-3.myhuaweicloud.com';
const iotdahttps = 'f5644079de.st1.iotda-app.cn-east-3.myhuaweicloud.com';

// ============== 图书查询 API 参数 ==============
const BOOK_API_KEY = 'ae1718d4587744b0b79f940fbef69e77';
const BOOK_API_URL = 'https://data.isbn.work/openApi/getInfoByIsbn';

// ============== OBS 配置 ==============
// 注：桶名需在华为云控制台创建后填入
const OBS_BUCKET = 'ka1shu1-alot';
const OBS_REGION = 'cn-east-3';
const OBS_ENDPOINT = 'obs.' + OBS_REGION + '.myhuaweicloud.com';

Page({
    // ============== 页面数据 ==============
    data: {
        // 连接状态
        isConnected: false,
        lastUpdateTime: '',

        // 传感器数据
        temperature: '--',
        photores: '--',
        humidity: 0,
        result: '等待订阅，请点击订阅按钮',

        // RGB灯状态（来自设备影子）
        red: 0,
        green: 0,
        blue: 0,

        // 图书相关
        bookUid: '',
        bookIsbn: '',
        bookStatus: '',
        bookTitle: '',
        bookAuthor: '',
        bookPublisher: '',
        bookYear: '',
        bookQueryStatus: 'idle',

        // 藏书统计（来自设备影子）
        bookCount: 0,
        onShelfCount: 0,
        borrowedCount: 0,
        recentEvents: [],

        // LED模拟状态
        ledStatus: {
            green: 'off',
            blue: 'off',
            red: 'off',
            yellow: 'off',
            network: 'off',
            temp: 'off'
        },

        // 调试日志
        consoleLog: '等待连接...\n',
        showLog: false,

        // 内部状态
        lastEventType: '',
        lastEventTime: 0,
        lastProcessedEvent: '',  // 去重：防止重复触发 OBS 写入
    },

    // ============== 按钮事件 ==============
    touchBtn_subTopic: function() {
        this.addLog('开始获取 Token...');
        this.getToken();
    },

    touchBtn_getshadow: function() {
        this.addLog('手动刷新设备影子...');
        this.getShadow();
    },

    touchBtn_setCommand: function() {
        this.addLog('下发 RGB 命令...');
        this.setCommand(1);
    },

    touchBtn_queryBook: function() {
        var isbn = this.data.bookIsbn;
        if (isbn && isbn.length > 0) {
            this.lookupBookByISBN(isbn);
        } else {
            wx.showToast({ title: '暂无 ISBN 可查询', icon: 'none' });
        }
    },

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
    slider1change: function(e) { RGBtemp[1] = e.detail.value; },
    slider2change: function(e) { RGBtemp[2] = e.detail.value; },
    slider3change: function(e) { RGBtemp[3] = e.detail.value; },
    slider4change: function(e) { RGBtemp[4] = e.detail.value; },

    // ============== 开关灯 ==============
    onClickOpen: function() {
        this.addLog('开灯命令下发中...');
        this.setCommand(1);
    },
    onClickOff: function() {
        this.addLog('关灯命令下发中...');
        this.setCommand(0);
    },

    // ============== 调试日志 ==============
    addLog: function(msg) {
        var time = new Date().toLocaleTimeString();
        var newLog = '[' + time + '] ' + msg + '\n';
        var currentLog = this.data.consoleLog;
        var logLines = (currentLog + newLog).split('\n');
        if (logLines.length > 30) {
            logLines = logLines.slice(-30);
        }
        this.setData({ consoleLog: logLines.join('\n') });
        console.log(msg);
    },

    clearLog: function() {
        this.setData({ consoleLog: '日志已清空\n' });
        this.addLog('日志已清空');
    },

    toggleLog: function() {
        this.setData({ showLog: !this.data.showLog });
    },

    // ============== 获取 Token ==============
    getToken: function() {
        var that = this;
        wx.request({
            url: 'https://' + iamhttps + '/v3/auth/tokens',
            data: JSON.stringify({
                auth: {
                    identity: {
                        methods: ["password"],
                        password: {
                            user: {
                                name: username,
                                password: password,
                                domain: { name: domainname }
                            }
                        }
                    },
                    scope: {
                        project: { name: "cn-east-3" }
                    }
                }
            }),
            timeout: 10000,
            method: 'POST',
            header: { 'content-type': 'application/json' },
            success: function(res) {
                var token = res.header['X-Subject-Token'];
                if (token) {
                    wx.setStorageSync('token', token);
                    that.addLog('Token 获取成功');
                    that.setData({ isConnected: true });
                    that.updateLedStatus();
                    that.getShadow();
                    that.startAutoRefresh();
                } else {
                    that.addLog('Token 获取失败');
                    that.setData({ isConnected: false });
                    that.updateLedStatus();
                }
            },
            fail: function(err) {
                that.addLog('Token 获取失败: ' + err.errMsg);
                that.setData({ isConnected: false });
                that.updateLedStatus();
            }
        });
    },

    // ============== 启动自动刷新 ==============
    startAutoRefresh: function() {
        if (shadowTimer) {
            clearInterval(shadowTimer);
        }
        var that = this;
        shadowTimer = setInterval(function() {
            that.getShadow();
        }, 5000);
        this.addLog('已启动自动刷新（每5秒）');
    },

    // ============== 获取设备影子（合并版：兼容两个设备的数据格式）==============
    getShadow: function() {
        var that = this;
        var token = wx.getStorageSync('token');
        if (!token) {
            this.addLog('Token 不存在，请先订阅');
            return;
        }

        wx.request({
            url: 'https://' + iotdahttps + '/v5/iot/' + projectId + '/devices/' + deviceId + '/shadow',
            method: 'GET',
            header: { 'content-type': 'application/json', 'X-Auth-Token': token },
            success: function(res) {
                if (!res.data.shadow || !res.data.shadow[0]) {
                    that.addLog('设备影子为空');
                    return;
                }
                that.parseShadowData(res.data);
                that.addLog('设备影子获取成功');
                that.setData({ lastUpdateTime: new Date().toLocaleString() });
            },
            fail: function(err) {
                if (err.statusCode === 401) {
                    that.addLog('Token 已过期，重新获取...');
                    that.getToken();
                } else {
                    that.addLog('获取设备影子失败: ' + (err.errMsg || err.statusCode));
                    that.setData({ isConnected: false });
                    that.updateLedStatus();
                }
            }
        });
    },

    // ============== 解析设备影子数据 ==============
    parseShadowData: function(data) {
        var reported = data.shadow[0].reported;
        var props = reported.properties || reported || {};

        // 传感器数据
        var temperature = props.Temperature;
        var photores = props.Photores;
        var humidity = props.Humidity || 0;
        var red = props.RED || 0;
        var green = props.GREEN || 0;
        var blue = props.BLUE || 0;

        // 图书数据
        var bookUid = props.book_uid || '';
        var bookIsbn = props.book_isbn || '';
        var bookStatus = props.book_status || '';

        // 藏书统计数据（优先使用 ESP32 上报的值）
        var bookCount = props.BookCount || 0;
        var onShelfCount = props.OnShelfCount || 0;
        var borrowedCount = props.BorrowedCount || 0;
        var bookList = props.BookList || [];
        var newEvents = props.RecentEvents || [];

        // 检测新事件
        var newEventType = null;
        if (newEvents && newEvents.length > 0) {
            var latestEvent = newEvents[0];
            if (latestEvent.type !== this.data.lastEventType) {
                newEventType = latestEvent.type;
                this.setData({ lastEventType: latestEvent.type });
                this.triggerEventBlink(latestEvent.type);
            }
        }

        var statusText = '温度' + temperature + ' 光照' + photores;
        if (bookIsbn) {
            statusText += ' | 书:' + bookIsbn + ' [' + bookStatus + ']';
        }

        this.setData({
            result: statusText,
            temperature: temperature !== undefined ? temperature.toFixed(1) + '°' : '--',
            photores: photores || '--',
            humidity: humidity,
            red: red,
            green: green,
            blue: blue,
            bookUid: bookUid,
            bookIsbn: bookIsbn,
            bookStatus: bookStatus,
            bookCount: bookCount,
            onShelfCount: onShelfCount,
            borrowedCount: borrowedCount,
            recentEvents: this.formatEvents(newEvents)
        });

        this.updateLedStatus();

        // 自动查询图书信息
        if (bookUid && bookUid !== lastBookUid && bookStatus === 'registered') {
            this.addLog('检测到新书注册，ISBN: ' + bookIsbn);
            lastBookUid = bookUid;
            wx.setStorageSync('lastBookUid', bookUid);
            this.lookupBookByISBN(bookIsbn);
            // 写入 OBS 注册记录
            this.obsRecordRegister(bookUid, bookIsbn, '', '', '');
        }

        // 借出/归还提示 + 写入 OBS（去重：相同 UID + Status 不重复触发）
        if (bookStatus === 'borrowed' || bookStatus === 'returned') {
            var eventKey = (bookUid || '') + ':' + bookStatus;
            if (eventKey !== this.data.lastProcessedEvent) {
                this.setData({ lastProcessedEvent: eventKey });
                wx.showToast({
                    title: '图书' + (bookStatus === 'borrowed' ? '已借出' : '已归还'),
                    icon: 'none',
                    duration: 2000
                });
                this.obsRecordBorrowReturn(bookStatus, bookUid, bookIsbn, this.data.bookTitle || '');
            }
        }
    },

    // ============== 格式化事件列表 ==============
    formatEvents: function(events) {
        if (!events || events.length === 0) return [];
        var that = this;
        return events.map(function(event) {
            var typeClass = event.type === 'returned' ? 'returned' : (event.type === 'taken' || event.type === 'borrowed' ? 'taken' : 'other');
            return {
                ...event,
                typeClass: typeClass,
                time: that.formatTime(event.timestamp)
            };
        });
    },

    formatTime: function(timestamp) {
        if (!timestamp) return '--';
        var date = new Date(timestamp);
        return (date.getMonth() + 1) + '/' + date.getDate() + ' ' +
            date.getHours().toString().padStart(2, '0') + ':' +
            date.getMinutes().toString().padStart(2, '0');
    },

    formatReadingTime: function(sec) {
        if (!sec || sec <= 0) return '0m';
        if (sec >= 3600) {
            return (sec / 3600).toFixed(1) + 'h';
        }
        return Math.round(sec / 60) + 'm';
    },

    // ============== 更新 LED 模拟状态 ==============
    updateLedStatus: function() {
        var isConnected = this.data.isConnected;
        var temperature = parseFloat(this.data.temperature);
        var now = Date.now();
        var lastEventTime = this.data.lastEventTime;

        var ledStatus = {
            green: 'off',
            blue: 'off',
            red: 'off',
            yellow: 'off',
            network: 'off',
            temp: 'off'
        };

        // 绿灯：系统正常待机时常亮
        if (isConnected) {
            ledStatus.green = 'static';
        }

        // 网络异常：红灯慢闪
        if (!isConnected) {
            ledStatus.network = 'blink-slow';
        }

        // 温度告警：红灯快闪（温度>30°C）
        if (!isNaN(temperature) && temperature > 30) {
            ledStatus.temp = 'blink-fast';
        }

        // 事件闪烁（500ms内保持）
        if (lastEventTime && (now - lastEventTime) < 500) {
            var lastEventType = this.data.lastEventType;
            if (lastEventType === 'returned') {
                ledStatus.blue = 'blink-once';
            } else if (lastEventType === 'taken' || lastEventType === 'borrowed') {
                ledStatus.red = 'blink-once';
            }
        }

        this.setData({ ledStatus: ledStatus });
    },

    // ============== 触发事件闪烁 ==============
    triggerEventBlink: function(eventType) {
        var that = this;
        this.setData({ lastEventTime: Date.now() });
        this.updateLedStatus();
        setTimeout(function() {
            that.updateLedStatus();
        }, 500);
        this.addLog('事件触发: ' + (eventType === 'returned' ? '图书归还' : (eventType === 'taken' || eventType === 'borrowed' ? '图书借出' : eventType)));
    },

    // ============== 跳转到书架页 ==============
    gotoBookshelf: function() {
        wx.navigateTo({ url: '/pages/bookshelf/bookshelf' });
    },

    // ============== 手动刷新 ==============
    manualRefresh: function() {
        this.addLog('手动刷新...');
        this.getShadow();
        wx.showToast({ title: '刷新中', icon: 'loading', duration: 1000 });
    },

    // ============== 测试LED闪烁 ==============
    testLedBlink: function() {
        this.addLog('测试LED闪烁');
        this.triggerEventBlink('returned');
        var that = this;
        setTimeout(function() {
            that.triggerEventBlink('taken');
        }, 800);
    },

    // ============== OBS 记录注册事件 ==============
    obsRecordRegister: function(uid, isbn, title, author, publisher) {
        var book = {
            uid: uid || '',
            isbn: isbn || '',
            title: title || isbn || '未知',
            author: author || '',
            publisher: publisher || '',
            currentStatus: 'on_shelf',
            borrowCount: 0,
            totalReadingSec: 0,
            lastBorrowTime: 0,
            history: []
        };
        this.obsPut('book/' + isbn + '.json', book);
        this.addLog('OBS 新增图书: ' + (title || isbn));
    },

    // ============== OBS 记录借还事件 ==============
    obsRecordBorrowReturn: function(type, uid, isbn, title) {
        var that = this;
        if (!isbn) return;

        this.obsGet('book/' + isbn + '.json', function(book) {
            if (!book) {
                // 如果 OBS 里没有这本书（小程序没注册过），直接写入
                var newBook = {
                    uid: uid || '',
                    isbn: isbn,
                    title: title || isbn,
                    currentStatus: type === 'borrowed' ? 'borrowed' : 'on_shelf',
                    borrowCount: type === 'borrowed' ? 1 : 0,
                    totalReadingSec: 0,
                    lastBorrowTime: type === 'borrowed' ? Date.now() : 0,
                    history: []
                };
                newBook.history.push({ type: type, timestamp: Date.now() });
                that.obsPut('book/' + isbn + '.json', newBook);
                that.addLog('OBS 新增借还记录: ' + (title || isbn));
                return;
            }

            var now = Date.now();
            book.history = book.history || [];

            if (type === 'borrowed') {
                book.currentStatus = 'borrowed';
                book.borrowCount = (book.borrowCount || 0) + 1;
                book.lastBorrowTime = now;
                book.history.push({ type: 'borrowed', timestamp: now });
            } else {
                // 归还：计算阅读时长
                book.currentStatus = 'on_shelf';
                if (book.lastBorrowTime && book.lastBorrowTime > 0) {
                    var durationSec = Math.round((now - book.lastBorrowTime) / 1000);
                    book.totalReadingSec = (book.totalReadingSec || 0) + durationSec;
                    book.history.push({
                        type: 'returned',
                        timestamp: now,
                        durationSec: durationSec
                    });
                } else {
                    book.history.push({ type: 'returned', timestamp: now });
                }
                book.lastBorrowTime = 0;
            }

            that.obsPut('book/' + isbn + '.json', book);
            that.addLog('OBS 更新: ' + (title || isbn) + ' [' + type + ']');
        });
    },

    // ============== OBS 更新图书详细信息（查询成功后调用）==============
    obsUpdateBookInfo: function(isbn, title, author, publisher) {
        if (!isbn) return;
        var that = this;
        this.obsGet('book/' + isbn + '.json', function(book) {
            if (!book) return;
            book.title = title || book.title;
            book.author = author || book.author;
            book.publisher = publisher || book.publisher;
            that.obsPut('book/' + isbn + '.json', book);
            that.addLog('OBS 更新书名: ' + title);
        });
    },

    // ============== OBS GET（公共桶，无认证）==============
    obsGet: function(key, callback) {
        var that = this;
        wx.request({
            url: 'https://' + OBS_BUCKET + '.' + OBS_ENDPOINT + '/' + key,
            method: 'GET',
            success: function(res) {
                if (res.statusCode === 200) {
                    try {
                        var data = typeof res.data === 'string' ? JSON.parse(res.data) : res.data;
                        callback(data);
                    } catch (e) {
                        callback(null);
                    }
                } else if (res.statusCode === 404) {
                    callback(null);
                } else {
                    that.addLog('OBS GET 失败: ' + res.statusCode);
                    callback(null);
                }
            },
            fail: function(err) {
                that.addLog('OBS GET 失败: ' + (err.errMsg || ''));
                callback(null);
            }
        });
    },

    // ============== OBS PUT（公共桶，无认证）==============
    obsPut: function(key, data, callback) {
        var that = this;
        var body = JSON.stringify(data, null, 0);
        var url = 'https://' + OBS_BUCKET + '.' + OBS_ENDPOINT + '/' + key;
        that.addLog('OBS PUT: ' + key + ' (' + body.length + 'B)');
        wx.request({
            url: url,
            method: 'PUT',
            data: body,
            header: { 'Content-Type': 'application/json' },
            success: function(res) {
                that.addLog('OBS PUT 响应: ' + res.statusCode);
                if (res.statusCode === 200 || res.statusCode === 201) {
                    if (callback) callback(true);
                } else {
                    that.addLog('OBS PUT 失败: ' + res.statusCode + ' ' + JSON.stringify(res.data).substring(0, 100));
                    if (callback) callback(false);
                }
            },
            fail: function(err) {
                that.addLog('OBS PUT 网络错误: ' + (err.errMsg || ''));
                if (callback) callback(false);
            }
        });
    },

    // ============== OBS 列举目录（公共桶，无认证）==============
    obsList: function(prefix, callback) {
        var that = this;
        wx.request({
            url: 'https://' + OBS_BUCKET + '.' + OBS_ENDPOINT + '/?prefix=' + encodeURIComponent(prefix),
            method: 'GET',
            dataType: 'text',
            success: function(res) {
                if (res.statusCode === 200 && typeof res.data === 'string') {
                    var files = [];
                    var regex = /<Contents>([\s\S]*?)<\/Contents>/g;
                    var match;
                    while ((match = regex.exec(res.data)) !== null) {
                        var content = match[1];
                        var keyMatch = content.match(/<Key>([\s\S]*?)<\/Key>/);
                        var timeMatch = content.match(/<LastModified>([\s\S]*?)<\/LastModified>/);
                        if (keyMatch && !keyMatch[1].endsWith('/')) {
                            files.push({
                                key: keyMatch[1],
                                lastModified: timeMatch ? timeMatch[1] : ''
                            });
                        }
                    }
                    files.sort(function(a, b) { return (b.lastModified || '').localeCompare(a.lastModified || ''); });
                    that.addLog('OBS 列举 ' + prefix + ': ' + files.length + ' 个文件');
                    callback(files);
                } else {
                    that.addLog('OBS 列举失败: ' + res.statusCode);
                    callback([]);
                }
            },
            fail: function(err) {
                that.addLog('OBS 列举网络错误: ' + (err.errMsg || ''));
                callback([]);
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
        this.addLog('查询图书: ' + isbn);

        wx.request({
            url: BOOK_API_URL,
            data: { isbn: isbn, appKey: BOOK_API_KEY },
            method: 'GET',
            success: function(res) {
                if (res.data.code === 0 && res.data.success && res.data.data) {
                    var book = res.data.data;
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
                    that.addLog('图书查询成功: ' + (book.bookName || isbn));

                    // 查询成功后，更新 OBS 中这本书的详细信息
                    that.obsUpdateBookInfo(isbn, book.bookName || '', book.author || '', book.press || '');

                    setTimeout(function() {
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
                    that.addLog('图书未找到: ' + isbn);
                }
            },
            fail: function(err) {
                that.setData({
                    bookQueryStatus: 'failed',
                    bookTitle: '查询失败',
                    result: '网络错误，请重试'
                });
                that.addLog('图书查询失败: ' + (err.errMsg || '网络错误'));
            }
        });
    },

    // ============== 下发图书信息命令到 ESP32 ==============
    sendBookInfoCommand: function(bookInfo) {
        var that = this;
        var token = wx.getStorageSync('token');
        if (!token) {
            this.addLog('无 Token，无法下发命令');
            return;
        }

        wx.request({
            url: 'https://' + iotdahttps + '/v5/iot/' + projectId + '/devices/' + deviceId + '/commands',
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
                that.addLog('图书信息已下发到设备');
                wx.showToast({ title: '已下发到设备', icon: 'success', duration: 1500 });
            },
            fail: function(err) {
                that.addLog('图书信息下发失败');
                wx.showToast({ title: '下发失败', icon: 'none' });
            }
        });
    },

    // ============== 下发 RGB 命令 ==============
    setCommand: function(key) {
        var that = this;
        var token = wx.getStorageSync('token');
        if (!token) {
            that.addLog('请先获取 Token');
            return;
        }

        wx.request({
            url: 'https://' + iotdahttps + '/v5/iot/' + projectId + '/devices/' + deviceId + '/commands',
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
                that.addLog('RGB 命令下发成功');
            },
            fail: function(err) {
                that.addLog('RGB 命令下发失败');
            }
        });
    },

    // ============== 生命周期 ==============
    onLoad: function(options) {
        this.addLog('页面加载完成');
        // 检查是否有缓存的 Token，有则直接刷新
        if (wx.getStorageSync('token')) {
            this.setData({ isConnected: true });
            this.getShadow();
            this.startAutoRefresh();
            this.updateLedStatus();
        }
    },

    onShow: function() {
        // 如果已有 Token，每5秒自动刷新已在 onLoad 启动
    },

    onHide: function() {
        if (shadowTimer) {
            clearInterval(shadowTimer);
            shadowTimer = null;
        }
    },

    onUnload: function() {
        if (shadowTimer) {
            clearInterval(shadowTimer);
            shadowTimer = null;
        }
    },

    onPullDownRefresh: function() {
        this.getShadow();
        setTimeout(function() {
            wx.stopPullDownRefresh();
        }, 1000);
    }
});
