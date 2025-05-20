import java.io.*;
import java.util.*;
import java.net.*;
import java.util.concurrent.ConcurrentHashMap;

public class server {

    static Vector<ClientHandler> array = new Vector<>();

    static int i = 0;

    public static void main(String[] args) throws IOException {
        ServerSocket ss = new ServerSocket(1234);

        Socket s;

        while (true) {
            s = ss.accept();

            System.out.println("New client request received : " + s);

            DataInputStream dis = new DataInputStream(s.getInputStream());
            DataOutputStream dos = new DataOutputStream(s.getOutputStream());

            System.out.println("Creating a new handler for this client...");

            String name = dis.readUTF();
            ClientHandler mtch = new ClientHandler(s, name, dis, dos);
            Thread t = new Thread(mtch);

            System.out.println("Adding this client to active clients");

            array.add(mtch);

            t.start();

            i++;
        }
    }
}

class ClientHandler implements Runnable {
    Scanner scn = new Scanner(System.in);
    private String name;
    final DataInputStream dis;
    final DataOutputStream dos;
    Socket s;
    boolean isloggedin;
    public static ConcurrentHashMap<String, Map<String, Set<ClientHandler>>> rooms = new ConcurrentHashMap<>();
    private Set<String> joinedRooms = Collections.newSetFromMap(new ConcurrentHashMap<>());
    private String currentRoom = null;

    public ClientHandler(Socket s, String name, DataInputStream dis, DataOutputStream dos) {
        this.dis = dis;
        this.dos = dos;
        this.name = name;
        this.s = s;
        this.isloggedin = true;
    }

    @Override
    public void run() {
        String received;

        try (FileWriter writer = new FileWriter("chat_log.txt", true);
                PrintWriter log = new PrintWriter(writer)) {

            while (isloggedin) {
                try {
                    received = dis.readUTF();
                    log.println(this.name + " to recipient: " + received);
                    System.out.println(received);

                    if (received.equals("logout")) {
                        this.isloggedin = false;
                        this.s.close();
                        break;
                    }

                    processMessage(received);

                } catch (IOException e) {
                    e.printStackTrace();
                    break;
                }
            }

        } catch (IOException e) {
            e.printStackTrace();
        } finally {
            try {
                this.dis.close();
                this.dos.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    private void processMessage(String received) {
        try {
            if (received.startsWith("/broadcast")) {
                String msg = received.substring(10);
                broadcast(msg);
                return;
            }
            if (received.equals("list")) {
                StringBuilder userList = new StringBuilder("Users:\n");
                for (ClientHandler mc : server.array) {
                    if (mc.isloggedin) {
                        userList.append(mc.name).append("\n");
                    }
                }
                dos.writeUTF(userList.toString());
            }
            if (received.startsWith("@")) {
                String[] split = received.split(" ", 2);
                String targetUser = split[0].substring(1);
                String message = split[1];

                for (ClientHandler ch : server.array) {
                    if (ch.name.equals(targetUser) && ch.isloggedin) {
                        ch.dos.writeUTF("[Private] " + this.name + ": " + message);
                    }
                }
            }

            if (received.equals("r/create")) {
                String roomname = "room_" + new Random().nextInt(10000);
                rooms.put(roomname, new ConcurrentHashMap<>());
                rooms.get(roomname).put("clients", Collections.newSetFromMap(new ConcurrentHashMap<>()));
                System.out.println("Room created: " + roomname);
                dos.writeUTF("Room created: " + roomname);
            }

            if (received.startsWith("r/join ")) {
                String[] parts = received.split(" ", 2);
                if (parts.length == 2) {
                    String roomasked = parts[1];
                    rooms.computeIfAbsent(roomasked, k -> new ConcurrentHashMap<>())
                            .computeIfAbsent("clients", k -> Collections.newSetFromMap(new ConcurrentHashMap<>()));
                    if (joinedRooms.add(roomasked)) {
                        rooms.get(roomasked).get("clients").add(this);
                        if (currentRoom == null) {
                            currentRoom = roomasked;
                        }
                        System.out.println(this.name + " joined " + roomasked);
                        dos.writeUTF("Joined room: " + roomasked);
                    } else {
                        dos.writeUTF("You are already in room: " + roomasked);
                    }
                } else {
                    dos.writeUTF("Invalid room join request. Use: r/join <roomname>");
                }
            }
            if (received.equals("r/list")) {
                StringBuilder roomList = new StringBuilder("Your joined rooms:\n");
                if (joinedRooms.isEmpty()) {
                    roomList.append("No rooms joined.");
                } else {
                    for (String room : joinedRooms) {
                        roomList.append(room);
                        if (room.equals(currentRoom)) {
                            roomList.append(" (current)");
                        }
                        roomList.append("\n");
                    }
                }
                dos.writeUTF(roomList.toString());
            }

            if (received.equals("r/whothere")) {
                if (currentRoom == null) {
                    dos.writeUTF("No current room selected. Use r/switch or r/join first.");
                    return;
                }
                StringBuilder clientList = new StringBuilder("Who are there in this room (" + currentRoom + "):\n");
                Map<String, Set<ClientHandler>> room = rooms.get(currentRoom);
                if (room == null || room.get("clients") == null || room.get("clients").isEmpty()) {
                    clientList.append("No one is in this room.");
                } else {
                    for (ClientHandler client : room.get("clients")) {
                        if (client.isloggedin) {
                            clientList.append(client.name).append("\n");
                        }
                    }
                }
                dos.writeUTF(clientList.toString());
            }

            if (received.startsWith("r/switch ")) {
                String[] parts = received.split(" ", 2);
                if (parts.length == 2) {
                    String roomName = parts[1];
                    if (joinedRooms.contains(roomName)) {
                        currentRoom = roomName;
                        dos.writeUTF("Switched to room: " + roomName);
                    } else {
                        dos.writeUTF("You are not in room: " + roomName);
                    }
                } else {
                    dos.writeUTF("Invalid switch request. Use: r/switch <roomname>");
                }
            }
            if (received.startsWith("r/leave ")) {
                String[] parts = received.split(" ", 2);
                if (parts.length == 2) {
                    String roomName = parts[1];
                    if (joinedRooms.remove(roomName)) {
                        rooms.get(roomName).get("clients").remove(this);
                        if (roomName.equals(currentRoom)) {
                            currentRoom = joinedRooms.isEmpty() ? null : joinedRooms.iterator().next();
                        }
                        System.out.println(this.name + " left " + roomName);
                        dos.writeUTF("Left room: " + roomName);
                        if (currentRoom != null) {
                            dos.writeUTF("Current room set to: " + currentRoom);
                        }
                    } else {
                        dos.writeUTF("You are not in room: " + roomName);
                    }
                } else {
                    dos.writeUTF("Invalid leave request. Use: r/leave <roomname>");
                }
            }
            if (received.startsWith("r/send ")) {
                if (currentRoom == null) {
                    dos.writeUTF("No current room selected. Use r/switch or r/join first.");
                    return;
                }
                String message = received.substring(7);
                sendToRoom(currentRoom, message);
            }

            if (received.startsWith("/file")) {
                StringTokenizer fileTokenizer = new StringTokenizer(received, "#");
                fileTokenizer.nextToken();
                String fileName = fileTokenizer.nextToken();

                byte[] buffer = new byte[4096];
                try (FileOutputStream fos = new FileOutputStream(fileName)) {
                    int bytesRead;
                    while ((bytesRead = dis.read(buffer)) != -1) {
                        fos.write(buffer, 0, bytesRead);
                        if (bytesRead < buffer.length)
                            break;
                    }
                    dos.writeUTF("File received: " + fileName);
                }
            }
            int N = 100;
            if (received.equals("history")) {
                try (Scanner sc = new Scanner(new File("chat_log.txt"))) {
                    while (sc.hasNextLine() && N > 0) {
                        dos.writeUTF(sc.nextLine());
                        N--;
                    }
                } catch (FileNotFoundException e) {
                    System.out.println("File not found!");
                    e.printStackTrace();
                }
            }

        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void sendToRoom(String roomName, String message) {
        Map<String, Set<ClientHandler>> room = rooms.get(roomName);
        if (room != null) {
            Set<ClientHandler> clients = room.get("clients");
            if (clients != null) {
                for (ClientHandler client : clients) {
                    if (client != this && client.isloggedin) {
                        try {
                            client.dos.writeUTF("[" + roomName + "] " + this.name + ": " + message);
                        } catch (IOException e) {
                            e.printStackTrace();
                        }
                    }
                }
            }
        }
    }

    private void broadcast(String message) {
        for (ClientHandler client : server.array) {
            if (client != this && client.isloggedin) {
                try {
                    client.dos.writeUTF("Broadcast from " + this.name + ": " + message);
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
        }
    }

}