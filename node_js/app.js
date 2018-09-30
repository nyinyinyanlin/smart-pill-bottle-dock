const express = require('express');
const app = express();
const schedule = require('node-schedule');
const MongoClient = require('mongodb').MongoClient;
const assert = require('assert');
const path = require('path');
const bodyParser = require('body-parser');

const port = process.env.PORT || 8080;
const url = process.env.DB_URL || 'mongodb://localhost:27017';
const dbName = process.env.DB_NAME || 'pillstation';

var db;
var schedules = [
  {
    "slotnum":"1",
    "job":{
      "time":null,
      "numofpills":null,
      "timeout":null,
      "status":null,//"pending",
    },
    "schedule":[
      //{"numofpills":20,"cron":"20 20 * * *","scheduler":obj},
    ]
  },
  {
    "slotnum":"2",
    "job":{
      "time":null,
      "numofpills":null,
      "timeout":null,
      "status":null,
    },
    "schedule":[
      //{"numofpills":20,"cron":"20 20 * * *","scheduler":obj},
    ]
  },
  {
    "slotnum":"3",
    "job":{
      "time":null,
      "numofpills":null,
      "timeout":null,
      "status":null,
    },
    "schedule":[
      //{"numofpills":20,"cron":"20 20 * * *","scheduler":obj},
    ]
  }
]

app.use(bodyParser.urlencoded({ extended: false }))
app.use(bodyParser.json())

app.use('/scripts/bootstrap/', express.static(__dirname + '/node_modules/bootstrap/dist/js/'));
app.use('/scripts/popper.js/', express.static(__dirname + '/node_modules/popper.js/dist/umd/'));
app.use('/scripts/jquery/', express.static(__dirname + '/node_modules/jquery/dist/'));
app.use('/styles/bootstrap/', express.static(__dirname + '/node_modules/bootstrap/dist/css/'));


MongoClient.connect(url, {useNewUrlParser: true}, function(err, client) {
  assert.equal(null, err);
  console.log("Connected successfully to server");
  db = client.db(dbName);
  populateSchedules(db);
});

const retrieveEvents = function(db,callback){
  const collection = db.collection('events');
  collection.find({}).toArray(function(err,docs){
    assert.equal(err,null);
    callback(docs);
  });
}

const insertEvent = function(data,db,callback){
  const collection = db.collection('events');
  collection.insertOne({"date":data.date, "slotnum" : data.slotnum, "timeofday" : data.timeofday, "description" :data.description },function(err, result) {
    assert.equal(err, null);
    assert.equal(1, result.result.n);
    assert.equal(1, result.ops.length);
    console.log("Inserted 1 event into the 'events' collection.");
    callback(result);
  });
}

const retrieveSchedules = function(db,callback){
  const collection = db.collection('schedules');
  collection.find({}).toArray(function(err,docs){
    assert.equal(err,null);
    callback(docs);
  });
}

const insertSchedule = function(data,db,callback){
  const collection = db.collection('schedules');
  collection.insertOne({"slotnum" : data.slotnum, "timeofday" : data.timeofday, "numofpills" :data.numofpills },function(err, result) {
    assert.equal(err, null);
    assert.equal(1, result.result.n);
    assert.equal(1, result.ops.length);
    console.log("Inserted 1 schedule into the 'schedules' collection.");
    callback(result);
  });
}

const removeSchedule = function(data,db,callback){
  const collection = db.collection('schedules');
  collection.deleteOne({"slotnum" : data.slotnum, "timeofday" : data.timeofday},function(err, result) {
    assert.equal(err, null);
    console.log("Removed 1 schedule from the 'schedules' collection.");
    callback(result);
  });
}

const populateSchedules = function(db){
  retrieveSchedules(db,function(docs){
    emptySchedules();
    docs.forEach(function(schedule){
      setSchedule(schedule.slotnum,schedule.numofpills,schedule.timeofday);
    })
  });
}

const emptySchedules = function(){
  schedules.forEach(function(schedule,index){
    schedules[index].job.time = null;
    schedules[index].job.numofpills = null;
    schedules[index].job.status = null;
    if(schedules[index].job.timeout){
      clearTimeout(schedules[index].job.timeout);
    }
    schedules[index].job.timeout = null;
    schedules[index].schedule.forEach(function(sch,i){
      if(schedules[index].schedule[i].scheduler){
        schedules[index].schedule[i].scheduler.cancel();
      }
    });
    schedules[index].schedule = [];
  });
}

const setSlotSetting = function(data,db,callback){
  const collection = db.collection('slotsetting');
  collection.updateOne({"slotnum" : data.slotnum},{$set:{"weight" : data.weight}},{upsert:true},function(err, result) {
    assert.equal(err, null);
    console.log("Updated 1 setting from the 'slotsetting' collection.");
    callback(result);
  });
}

const retrieveSlotSettings = function(db,callback){
  const collection = db.collection('slotsetting');
  collection.find({}).toArray(function(err,docs){
    assert.equal(err,null);
    callback(docs);
  });
}

const timeToCron = function(time){
  cron = " * * *";
  hh = parseInt(time.slice(0,2));
  mm = parseInt(time.slice(3,5));
  cron = mm.toString() + " " +hh.toString() +  cron;
  return cron;
}

const setJobStatus = function(slotnum,status,callback){
  var index = schedules.findIndex(
    function(schedule){
      return schedule.slotnum === slotnum;
    }
  );
  if(index>=0){
    schedules[index].job.status = status;
    callback(slotnum,index);
  }else{
    callback(null,-1);
  }
}

const endJob = function(slotnum,status,callback){
  var index = schedules.findIndex(function(schedule){
    return schedule.slotnum === slotnum;
  });
  if(index>=0){
    var description = "";
    if(status==="success"){
      description = "Patient took the correct amount of pills from Slot "+slotnum+".";
    }else if(status==="less"){
      description = "Patient took less amount of pills than he/she must from Slot "+slotnum+".";
    }else if(status==="more"){
      description = "Patient took more pills than he/she must from Slot "+slotnum+".";
    }else if(status==="timeout"){
      description = "Patient did not take pills from Slot "+slotnum+" in time.";
    }
    insertEvent({"date":(new Date()).toTimeString(), "slotnum" : slotnum, "timeofday" :  schedules[index].job.time, "description" : description},db,function(result){
      callback(index,result);
    });
  }
}

const setSchedule = function(slotnum,numofpills,time){
  var index = schedules.findIndex(function(schedule){
    return schedule.slotnum === slotnum;
  });
  if(index>=0){
    schedules[index].schedule.push({
      "numofpills":numofpills,
      "cron":timeToCron(time),
      "scheduler":schedule.scheduleJob(timeToCron(time), function(){
        var slotnum_sch = slotnum;
        var index_sch = schedules.findIndex(function(schedule){
          return schedule.slotnum === slotnum_sch;
        });
        if(index_sch>=0){
          schedules[index_sch].job.time = time;
          schedules[index_sch].job.numofpills = numofpills;
          schedules[index_sch].job.status = "pending";
          schedules[index_sch].job.timeout = setTimeout(function(){
            endJob(slotnum_sch,"timeout",function(i,result){
              console.log("Job timeout from Slot "+i+","+result);
              schedules[i].job.time = null;
              schedules[i].job.numofpills = null;
              schedules[i].job.status = null;
              schedules[i].job.timeout = null;
            });
          },300000);
          console.log("Time to take "+ numofpills +" from Slot "+slotnum);
        }
      })
    });
  }
}

app.get('/',function(req,res){
  res.sendFile(path.join(__dirname+ '/static/dashboard.html'));
});

app.get('/getevents',function(req,res){
  console.log("/getevents");
  retrieveEvents(db,function(docs){
    res.json({'events':docs});
  });
});

app.get('/insertevent',function(req,res){
  console.log("/insertevent");
  insertEvent({"date": req.query["date"], "slotnum" : req.query["slotnum"], "timeofday" :  req.query["timeofday"], "description" : req.query["description"]},db,function(result){
    res.json({'result':result});
  });
});

app.get('/getschedules',function(req,res){
  console.log("/getschedules");
  retrieveSchedules(db,function(docs){
    res.json({'schedules':docs});
  });
});

app.post('/insertschedule',function(req,res){
  console.log("/insertschedule");
  insertSchedule({"slotnum" : req.body["slotnum"], "timeofday" :  req.body["timeofday"], "numofpills" : req.body["numofpills"]},db,function(result){
    populateSchedules(db);
    res.json({'result':result});
  });
});

app.post('/removeschedule',function(req,res){
  console.log("/removeschedule");
  removeSchedule({"slotnum" : req.body["slotnum"], "timeofday" :  req.body["timeofday"]},db,function(result){
    populateSchedules(db);
    res.json({'result':result});
  });
});

app.post('/setpillslot',function(req,res){
  console.log("/setpillslot");
  setSlotSetting({"slotnum" : req.body["slotnum"], "weight" :  req.body["weight"]},db,function(result){
    res.json({'result':result});
  });
});

app.get('/getsetting',function(req,res){
  console.log("/getsetting");
  retrieveSlotSettings(db,function(docs){
    res.json({'settings':docs});
  })
});

app.get('/getsettinghw',function(req,res){
  console.log("/getsettinghw");
  retrieveSlotSettings(db,function(docs){
    var response = "";
    docs.forEach(function(slot){
      response+=slot.slotnum+"-"+slot.weight+","
    });
    res.send(response.slice(0,-1));
  })
});

app.get('/getjobs',function(req,res){
  console.log("/getjobs");
  var response = "";
  schedules.forEach(function(slot){
    if(slot.job.status==="pending"){
      response+=slot.slotnum+"-"+slot.job.numofpills+",";
    }
  });
  res.send(response.slice(0,-1));
});

app.get('/setjobstatus',function(req,res){
  console.log("/setjobstatus");
  slot = req.query["slotnum"];
  status = req.query["status"];
  setJobStatus(slot,status,function(slotnum,index){
    res.send(slotnum+","+index);
  });
});

app.get('/endjob',function(req,res){
  console.log("/endjob");
  slotnum = req.query["slotnum"];
  status = req.query["status"];
  endJob(slotnum,status,function(index,result){
    schedules[index].job.time = null;
    schedules[index].job.numofpills = null;
    schedules[index].job.status = null;
    clearTimeout(schedules[index].job.timeout);
    schedules[index].job.timeout = null;
    res.send("");
  })
});

app.listen(port);
