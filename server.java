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

            ClientHandler mtch = new ClientHandler(s, "client " + i, dis, dos);
            Thread t = new Thread(mtch);

            System.out.println("Adding this client to active client list");

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

            if (received.equals("r/create")) {
                String roomname = "room_" + new Random().nextInt(10000);
                rooms.put(roomname, new ConcurrentHashMap<>());
                System.out.println("Room created: " + roomname);
                dos.writeUTF("Room created: " + roomname);
            }

            if (received.startsWith("r/join ")) {
                String[] parts = received.split(" ");
                if (parts.length == 2) {
                    String roomasked = parts[1];
                    rooms.computeIfAbsent(roomasked, k -> new ConcurrentHashMap<>())
                            .computeIfAbsent("clients", k -> Collections.newSetFromMap(new ConcurrentHashMap<>()))
                            .add(this);
                    System.out.println(this.name + " joined " + roomasked);
                } else {
                    System.out.println("Invalid room join request.");
                }
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
                        System.out.println(sc.nextLine());
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
