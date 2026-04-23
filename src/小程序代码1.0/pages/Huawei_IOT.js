// pages/Huawei_IOT.js

//设置温度值和湿度值 
var Temp = null;
var Phot = null;
var RED = '';
var GREEN = '';
var BLUE = '';
var RGBtemp = [0, 0, 0, 0, 0];
var autoRefreshTimer = null;
// 设置用户登录参数，此处需要全部修改替换为真实参数
const      domainname = 'altokra';
const      username = 'alto';
const      password = 'wwdx,wztx121';
const      projectId = '08250adf11f8442caf21bcee040d6c05';
const      deviceId = '69ae7d56e094d615922474f7_exp2';
const      iamhttps = 'iam.cn-east-3.myhuaweicloud.com';//在华为云连接参数中获取
const      iotdahttps = 'f5644079de.st1.iotda-app.cn-east-3.myhuaweicloud.com';//在华为云连接参数中获取

Page({
    /**
     * 页面的初始数据
     */
    data: {
        result:'等待订阅，请点击 订阅按钮',
    //设置温度值和湿度值 
        temperature: "",
        photores: "",
    },
    
    /**
     * 订阅按钮按下：
     */
    touchBtn_subTopic:function() {
      console.log("订阅按钮按下");
      this.setData({result:'订阅按钮按下'});
      this.gettoken();
    },
    /**
     * 获取设备影子按钮按下：
     */
    touchBtn_getshadow:function()
    {
        console.log("获取设备影子按钮按下");
        this.setData({result:'获取设备影子按钮按下'});
        this.getshadow();
    },
    /**
     * 设备命令下发按钮按下：
     */
    touchBtn_setCommand:function()
    {
        console.log("设备命令下发按钮按下");
        this.setData({result:'设备命令下发按钮按下，正在下发。。。'});
        this.setCommand(1);
    },  
    
    /**
     * 配置四个滑动条的功能：
     */
    slider1change: function (e) {  //RED
      RGBtemp[1] = e.detail.value;
      console.log(`需要发送的RGB数据分别为：`, 'R:', RGBtemp[1], 'G:', RGBtemp[3], 'B:', RGBtemp[2], 'fre:', RGBtemp[4]);
      console.log(`slider1发生change事件，携带值为`, e.detail.value);
    },
  
    slider2change: function (e) {  //BLUE
      RGBtemp[2] = e.detail.value;
      console.log(`需要发送的RGB数据分别为：`, 'R:', RGBtemp[1], 'G:', RGBtemp[3], 'B:', RGBtemp[2], 'fre:', RGBtemp[4]);
      console.log(`slider2发生change事件，携带值为`, e.detail.value);
    },
    slider3change: function (e) { //GREEN
      RGBtemp[3] = e.detail.value;
      console.log(`需要发送的RGB数据分别为：`, 'R:', RGBtemp[1], 'G:', RGBtemp[3], 'B:', RGBtemp[2], 'fre:', RGBtemp[4]);
      console.log(`slider3发生change事件，携带值为`, e.detail.value);
    },
  
    slider4change: function (e) {  //Fre
      RGBtemp[4] = e.detail.value;
      console.log(`需要发送的RGB数据分别为：`, 'R:', RGBtemp[1], 'G:', RGBtemp[3], 'B:', RGBtemp[2], 'fre:', RGBtemp[4]);
      console.log(`slider3发生change事件，携带值为`, e.detail.value);
    },

    /**
     * 开关灯按钮：
     */
    onClickOpen() {
      console.log("开灯按钮按下");
      this.setData({result:'开灯按钮按下，正在下发。。。'});
      this.setCommand(1);
    },
    onClickOff() {
      console.log("关灯按钮按下");
      this.setData({result:'关灯按钮按下，正在下发。。。'});
      this.setCommand(0);
    },

    /**
     * 获取token
     */
    gettoken:function(){
        console.log("开始获取。。。");//打印完整消息
        var that=this;  //这个很重要，在下面的回调函数中由于异步问题不能有效修改变量，需要用that获取
        wx.request({
            url: `https://${iamhttps}/v3/auth/tokens`,
            data:`{"auth": { "identity": {"methods": ["password"],"password": {"user": {"name": "${username}","password": "${password}","domain": {"name": "${domainname}"}}}},"scope": {"project": {"name": "cn-north-4"}}}}`,
            method: 'POST', // OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE, CONNECT
            header: {'content-type': 'application/json' }, // 请求的 header 
            success: function(res){// success
              // success
                console.log("获取token成功");//打印完整消息
                console.log(res);//打印完整消息
                var token='';
                token=JSON.stringify(res.header['X-Subject-Token']);//解析消息头的token
                token=token.replaceAll("\"", "");
                console.log("获取token=\n"+token);//打印token
                wx.setStorageSync('token',token);//把token写到缓存中,以便可以随时随地调用
            },
            fail:function(){
                // fail
                console.log("获取token失败");//打印完整消息
            },
            complete: function() {
                // complete
                console.log("获取token完成");//打印完整消息
            } 
        });
    },
    /**
     * 获取设备影子
     */
    getshadow:function(){
        console.log("开始获取影子");//打印完整消息
        var that=this;  //这个很重要，在下面的回调函数中由于异步问题不能有效修改变量，需要用that获取
        var token=wx.getStorageSync('token');//读缓存中保存的token
        wx.request({
            url: `https://${iotdahttps}/v5/iot/${projectId}/devices/${deviceId}/shadow`,
            data:'',
            method: 'GET', // OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE, CONNECT
            header: {'content-type': 'application/json','X-Auth-Token':token }, //请求的header 
            success: function(res){// success
            // success
                console.log(res);//打印完整消息
                var shadow=JSON.stringify(res.data.shadow[0].reported.properties);
                console.log('设备影子数据：'+shadow);
                //以下根据自己的设备属性进行解析
                //我的设备影子：{"Temperature":25.36,"RED":15,"GREEN":50,"BLUE":250}
                Temp=JSON.stringify(res.data.shadow[0].reported.properties.Temperature);
                Phot=JSON.stringify(res.data.shadow[0].reported.properties.Photores);
                RED=JSON.stringify(res.data.shadow[0].reported.properties.RED);
                GREEN=JSON.stringify(res.data.shadow[0].reported.properties.GREEN);
                BLUE=JSON.stringify(res.data.shadow[0].reported.properties.BLUE);
                console.log('温度='+Temp+'℃');
                console.log('光照='+Phot);
                console.log('红色='+RED);
                console.log('绿色='+GREEN);
                console.log('蓝色='+BLUE);
                that.setData({result:'温度'+Temp+'℃,光照'+Phot +'红色'+RED+'绿色'+GREEN+'蓝色'+BLUE});
                that.setData({temperature:Temp,});
                that.setData({photores:Phot,});
            },
            fail:function(){
                // fail
                console.log("获取影子失败");//打印完整消息
                console.log("请先获取token");//打印完整消息
            },
            complete: function() {
                // complete
                console.log("获取影子完成");//打印完整消息
            } 
        });
    },
    /**
     * 设备命令下发
     */
    setCommand:function(key){
        console.log("开始下发命令。。。");//打印完整消息
        var that=this;  //这个很重要，在下面的回调函数中由于异步问题不能有效修改变量，需要用that获取
        var token=wx.getStorageSync('token');//读缓存中保存的token
        wx.request({
            url: `https://${iotdahttps}/v5/iot/${projectId}/devices/${deviceId}/commands`,
            // data:'{"service_id": "Arduino","command_name": "RGB","paras": { "RED":100,"GREEN":100, "BLUE":100,"Switch":1}}',
            data:`{"service_id": "Arduino","command_name": "RGB","paras": { "RED": ${RGBtemp[1]},"GREEN":${RGBtemp[3]}, "BLUE":${RGBtemp[2]},"Switch":${key}}}`,
            method: 'POST', // OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE, CONNECT
            header: {'content-type': 'application/json','X-Auth-Token':token }, //请求的header 
            success: function(res){// success
                // success
                console.log("下发命令成功");//打印完整消息
                console.log(res);//打印完整消息
                
            },
            fail:function(){
                // fail
                console.log("命令下发失败");//打印完整消息
                console.log("请先获取token");//打印完整消息
            },
            complete: function() {
                // complete
                console.log("命令下发完成");//打印完整消息
                that.setData({result:'设备命令下发完成'});
            } 
        });
    },

      
    /**
     * 生命周期函数--监听页面加载
     */
    onLoad(options) {

    },

    /**
     * 生命周期函数--监听页面初次渲染完成
     */
    onReady() {

    },

    /**
     * 生命周期函数--监听页面显示
     */
    onShow() {
        // 启动自动刷新，每 5 秒获取一次设备影子
        autoRefreshTimer = setInterval(() => {
            this.getshadow();
        }, 5000);
    },

    /**
     * 生命周期函数--监听页面隐藏
     */
    onHide() {
        // 页面隐藏时停止自动刷新
        if (autoRefreshTimer) {
            clearInterval(autoRefreshTimer);
            autoRefreshTimer = null;
        }
    },

    /**
     * 生命周期函数--监听页面卸载
     */
    onUnload() {
        // 页面卸载时停止自动刷新
        if (autoRefreshTimer) {
            clearInterval(autoRefreshTimer);
            autoRefreshTimer = null;
        }
    },

    /**
     * 页面相关事件处理函数--监听用户下拉动作
     */
    onPullDownRefresh() {

    },

    /**
     * 页面上拉触底事件的处理函数
     */
    onReachBottom() {

    },

    /**
     * 用户点击右上角分享
     */
    onShareAppMessage() {

    }
})