// ==========================================
// 🔥 LOVEBOX CORE DASHBOARD JAVASCRIPT v3.6.0
// ==========================================

const firebaseConfig = {
  apiKey: "AIzaSyDV9Ljyb_D3km2q8QkebyxU_XXMPv5KWQk",
  authDomain: "lovebox-2507.firebaseapp.com",
  databaseURL: "https://lovebox-2507-default-rtdb.firebaseio.com",
  projectId: "lovebox-2507",
  storageBucket: "lovebox-2507.firebasestorage.app",
  messagingSenderId: "972714934837",
  appId: "1:972714934837:web:6d63567c01c45a6ffc277c"
};

firebase.initializeApp(firebaseConfig);
const database = firebase.database();

let currentUser = ""; 
let lastHeartbeatTime = Date.now(); 
let currentBoxTypingState = false; 
let currentWebTypingState = "";
window.boxTypingTimer = null; 
window.renderedMessages = new Set(); // Prevent duplicates

window.onload = function() {
    let savedUser = localStorage.getItem("lovebox_user");
    if (savedUser) {
        currentUser = savedUser;
        initChatSystem();
    } else {
        askForUser();
    }
    
    // Notification setup
    if (Notification.permission !== "granted") {
        Notification.requestPermission();
    }
};

function askForUser() {
    let userChoice = prompt("Who are you?\nType '1' for Gaurav\nType '2' for Partner");
    if (userChoice === "1" || userChoice === "2") {
        currentUser = (userChoice === "1") ? "Gaurav" : "Partner";
        localStorage.setItem("lovebox_user", currentUser);
        initChatSystem();
    } else {
        alert("Invalid choice! Please refresh.");
        askForUser();
    }
}

function playNotification() {
    const audio = document.getElementById("notifSound");
    if(audio) audio.play().catch(e => console.log("Audio blocked"));
}

function initChatSystem() {
    console.log("Logged in as: " + currentUser);
    
    // Initial Status
    database.ref("partner_last_seen").set("Active");

    // Chat Listener with Duplicate Prevention
    database.ref("messages").on("child_added", function(snapshot) {
        const key = snapshot.key;
        if (window.renderedMessages.has(key)) return;
        window.renderedMessages.add(key);

        let data = snapshot.val();
        let chat = document.getElementById("chatBox");

        // Clear welcome msg
        if(chat.querySelector(".receive") && chat.querySelector(".receive").innerText.includes("Welcome")) {
            chat.innerHTML = "";
        }

        let messageClass = data.sender === currentUser ? "send" : "receive";
        chat.innerHTML += `
        <div class="${messageClass}" id="msg-${key}">
            <strong>${data.sender}:</strong><br>
            ${data.text}
            <span>${data.time}</span>
        </div>
        `;
        chat.scrollTop = chat.scrollHeight;

        if (data.sender !== currentUser) playNotification();
    });

    // Lid Status
    database.ref("lid_status").on("value", (snapshot) => {
        let lidElement = document.getElementById("lidStatus");
        if(lidElement) lidElement.innerHTML = snapshot.val() === "open" ? `📦 LoveBox : Open ❤️` : `📦 LoveBox : Closed 💤`;
    });

    // Online Status & Heartbeat
    database.ref("box_status").on("value", (snapshot) => {
        let statusBadge = document.getElementById("onlineBadge");
        lastHeartbeatTime = Date.now();
        if (statusBadge) {
            const isOnline = snapshot.val() === "online" || snapshot.val() == 1;
            statusBadge.innerHTML = isOnline ? "🟢 Online" : "🔴 Offline";
            statusBadge.style.color = isOnline ? "#2ec4b6" : "#e63946";
        }
    });

    // Battery
    database.ref("box_battery_pct").on("value", (s) => {
        const el = document.getElementById("batteryDisplay");
        if(el) el.innerHTML = `🔋 Battery: ${s.val()}%`;
    });

    // Box Last Seen
    database.ref("box_last_seen").on("value", (s) => {
        const el = document.getElementById("last-seen-display");
        if(el) el.innerHTML = `🕒 Box Last Seen: ${s.val()} ✨`;
    });

    // Typing Logic
    database.ref("typing_status").on("value", (s) => {
        currentWebTypingState = s.val() || "";
        renderTypingUI();
    });

    database.ref("box_typing").on("value", (s) => {
        currentBoxTypingState = !!s.val(); 
        renderTypingUI();
    });

    // Reply Logic
    database.ref("box_reply").on("value", (s) => {
        let reply = s.val();
        if (reply && reply !== "Sent!") {
            // Logic handled by child_added listener via database push
            database.ref("box_reply").set("Sent!"); 
        }
    });

    // Watchdog
    setInterval(() => {
        if (Date.now() - lastHeartbeatTime > 20000) {
            document.getElementById("onlineBadge").innerHTML = "🔴 Offline";
        }
    }, 5000);
}

function renderTypingUI() {
    const typingDiv = document.getElementById("typing");
    if (!typingDiv) return;
    
    if (currentBoxTypingState) {
        typingDiv.innerHTML = `💬 Box Partner is typing...`;
    } else if (currentWebTypingState !== "" && currentWebTypingState.includes("is typing")) {
        typingDiv.innerHTML = `💬 ${currentWebTypingState}`;
    } else {
        typingDiv.innerHTML = "❤️ Waiting...";
    }
}

// Commands
function triggerHardwareBeep() { database.ref("server_command").set("trigger_beep"); }
function triggerTitanicTheme() { database.ref("server_command").set("trigger_titanic"); }
function spinHeart() { database.ref("server_command").set("trigger_servo"); }

function sendMessage(){
    let input = document.getElementById("message");
    let msg = input.value.trim();
    if(msg == "" || currentUser == "") return;

    let updatePayload = {
        [`/messages/${Date.now()}`]: { sender: currentUser, text: msg, time: getTime() },
        "/latest_message": msg,
        "/message_status": "unseen",
        "/typing_status": "Sent!",
        "/web_typing": false
    };

    database.ref().update(updatePayload);
    input.value = "";
}

function clearChat() {
    if(!confirm("Clear everything?")) return;
    const updates = {
        "/messages": null,
        "/latest_message": null,
        "/typing_status": null,
        "/box_reply": null,
        "/message_status": null,
        "/web_typing": false,
        "/box_typing": false
    };
    database.ref().update(updates);
    document.getElementById("chatBox").innerHTML = "";
    window.renderedMessages.clear();
}

function toggleSleepMode() {
    database.ref("server_command").set("sleep_mode");
    alert("LoveBox entering Deep Sleep.");
}

function restartBox() { if(confirm("Restart?")) database.ref("server_command").set("restart"); }

function getTime(){
    let d = new Date();
    return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

document.getElementById("message").addEventListener("input", function(){
    database.ref("typing_status").set(currentUser + " is typing...");
    database.ref("web_typing").set(true);
    clearTimeout(window.typingTimer);
    window.typingTimer = setTimeout(() => {
        database.ref("typing_status").set("");
        database.ref("web_typing").set(false);
    }, 1500);
});

document.getElementById("message").addEventListener("keypress", (e) => { if(e.key === 'Enter') sendMessage(); });